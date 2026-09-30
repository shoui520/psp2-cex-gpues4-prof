import csv
import importlib.util
import io
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "analyze", Path(__file__).resolve().parents[1] / "tools/analyze.py")
reader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reader)


class CaptureReaderTests(unittest.TestCase):
    def setUp(self):
        self.events = []

    def event(self, kind=17, call=0, context=1, args=(), **fields):
        e = dict.fromkeys(reader.COLUMNS, 0)
        e.update(label="", kind=kind, object=call, context=context, thread=1,
                 sequence=len(self.events), before_us=len(self.events)*10,
                 after_us=len(self.events)*10+1)
        e.update({f"arg{i}": v for i, v in enumerate(args)})
        e.update(fields)
        self.events.append(e)
        return e

    def setup_draw(self):
        self.event(call=9)
        self.event(call=11, context=0, args=(5, 10, 100))
        self.event(call=12, context=0, args=(5, 20, 200))
        self.event(call=7, args=(100,))
        self.event(call=8, args=(200,))
        self.event(call=1)

    def test_mask_update_fragment_lifetime(self):
        self.event(call=9)
        self.event(call=11, context=0, args=(5, 10, 100))
        self.event(call=25, context=0, args=(5, 0, 200))
        self.event(call=7, args=(100,))
        self.event(call=8, args=(200,))
        self.event(call=1)
        self.event(call=4)
        self.event(call=2)
        result = reader.summarize(self.events)
        self.assertFalse(result['warnings'])
        self.assertEqual(result['draws'][0]['fragment_shader']['name'], 'GXM mask update')

    def timing_fixture(self):
        self.event(kind=24, context=0, args=(365, 7, 0, 0))
        self.setup_draw()
        self.event(call=4)
        self.event(call=4)
        self.event(call=2)
        base = reader.summarize(self.events)
        def sample(i, batch):
            raw = (1 << 29) | (batch << 16)
            return dict(thread=4, sample_id=i, before_us=1000+i*1000,
                        after_us=1010+i*1000, result=0, process_id=7,
                        clock_before_mhz=111, clock_after_mhz=111,
                        driver_fingerprint=0xc0f361a3,
                        cores=[dict(core=c, scheduler_before=[7,0,0,7,0,0],
                                    scheduler_after=[7,0,0,7,0,0],
                                    pds_before=raw, pds_after=raw, flags=33,
                                    timer_before=10000+i*6920, timer_after=10020+i*6920)
                               for c in range(4)])
        return base, [sample(0,1), sample(1,2)]

    def test_sampled_timing_conservation(self):
        base,samples = self.timing_fixture()
        result = reader.sampled_timing(self.events,base['draws'],base['scenes'],samples,0)
        self.assertTrue(result['available'])
        self.assertEqual(result['matched_core_observations'],8)
        self.assertAlmostEqual(sum(p['estimated_ms'] for p in result['passes']),1,places=3)
        self.assertEqual(result['shaders'][0]['endpoint_sensitivity_ms'],0)
        for d in base['draws']:
            self.assertAlmostEqual(d['sampled_fragment_ms'],.5,places=3)
            self.assertIsNone(d['gpu_time_us'])

    def test_text_report_layout(self):
        base,samples = self.timing_fixture()
        base['sampled_fragment_timing'] = reader.sampled_timing(
            self.events,base['draws'],base['scenes'],samples,0)
        timing = base['sampled_fragment_timing']
        timing['passes'] = [dict(name='Small', estimated_ms=1),
                            dict(name='Large', estimated_ms=1234.5)]
        for draw, name, cost in zip(base['draws'], ['Small', 'Large'], [1, 1234.5]):
            draw.update({'pass': name, 'sampled_fragment_ms': cost,
                         'name': 'unsafe\x1b[31m'})
        import copy
        original = copy.deepcopy(base)
        text = reader.format_report(base)
        self.assertLess(text.index('Large'),text.index('Small'))
        self.assertIn('1,234.500 ms',text)
        self.assertIn('capture totals',text)
        self.assertIn('Estimated timings. Unsampled work is excluded.',text)
        self.assertIn('Draws without timing',text)
        self.assertNotIn('\x1b',text)
        self.assertLess(text.index('Large'),text.index('Recorded events'))
        self.assertIn('Most expensive draw groups', text)
        self.assertIn('Fragment shaders', text)
        self.assertIn('Timed draws', text)
        self.assertIn('Sample gap · max', text)
        self.assertIn('unsafe\\x1b[31m', text)
        self.assertEqual(base, original)

    def test_text_report_limits_and_unknown_cost(self):
        import copy
        base, samples = self.timing_fixture()
        base['sampled_fragment_timing'] = reader.sampled_timing(
            self.events, base['draws'], base['scenes'], samples, 0)
        template = base['draws'][0]
        for i in range(10):
            draw = copy.deepcopy(template)
            draw.update(name=f'Untimed {i}', sampled_fragment_ms=None)
            draw['fragment_shader'].update(handle=300+i, name=f'Shader {i}')
            base['draws'].append(draw)
        text = reader.format_report(base)
        self.assertIn('… 3 more in --json', text)
        row = next(line for line in text.splitlines() if ' / Shader 0' in line)
        self.assertIn('—', row)
        self.assertIn('0/1', row)
        self.assertNotIn('0.000 ms', row)

    def test_text_report_without_timing(self):
        result = reader.summarize([])
        text = reader.format_report(result)
        self.assertIn('Fragment timing unavailable',text)
        self.assertIn('missing explicit GXM identity seed',text)
        self.assertNotIn('█',text)

    def pipeline_fixture(self, group=71):
        base, samples = self.timing_fixture()
        for sample in samples:
            sample.update(group=group, tag_group=70, abi=1)
            for core in sample['cores']:
                core.update(tag_before=core['pds_before'], tag_after=core['pds_after'],
                            flags=1, value=0)
        base['diagnostics'] = samples
        base['sampled_fragment_timing'] = reader.sampled_timing(
            self.events, base['draws'], base['scenes'], samples, 0)
        return base

    def metric(self, analysis, name):
        return next(m for m in analysis['pipeline_activity']['metrics'] if m['id'] == name)

    def test_pipeline_rates_units_and_missing_metrics(self):
        base = self.pipeline_fixture()
        base['diagnostics'][0]['cores'][0]['value'] = (1 << 3) | 1
        result = reader.agent_report(base)
        metric = self.metric(result, 'usse_running')
        self.assertEqual(metric['asserted'], 1)
        self.assertEqual(metric['observed'], 8)
        self.assertEqual(metric['sample_hit_pct'], 12.5)
        self.assertEqual(metric['covered_units'], 4)
        self.assertEqual(metric['expected_units'], 16)
        self.assertEqual(sum(u['observed'] for u in metric['units']), 8)
        self.assertIsNone(self.metric(result, 'texture_l1_l2_stall')['sample_hit_pct'])
        self.assertEqual(self.metric(result, 'usse_stalled')['sample_hit_pct'], 0)
        self.assertFalse(result['pipeline_activity']['exclusive_draw_ownership'])
        text = reader.format_report(base)
        self.assertIn('12.5%', text)
        self.assertIn('No matched samples: Shader waits, Texture / data cache', text)

    def test_pipeline_correlations_keep_per_pass_denominators(self):
        base = self.pipeline_fixture()
        base['draws'][0]['pass'] = 'First'
        base['draws'][1]['pass'] = 'Second'
        for core in base['diagnostics'][0]['cores']:
            core['value'] = 1 << 3
        activity = reader.agent_report(base)['pipeline_activity']
        passes = {p['name']: p for p in activity['by_pass']}
        def running(p):
            return next(m for m in p['metrics'] if m['id'] == 'usse_running')
        self.assertEqual(running(passes['First'])['sample_hit_pct'], 100)
        self.assertEqual(running(passes['Second'])['sample_hit_pct'], 0)
        self.assertEqual(running(passes['First'])['observed'], 4)
        self.assertEqual(running(activity['by_fragment_shader'][0])['observed'], 8)

    def test_pipeline_keeps_stage_disagreement_without_exclusive_attribution(self):
        base = self.pipeline_fixture()
        for sample in base['diagnostics']:
            for core in sample['cores']:
                core.update(tag_before=0x20090000, tag_after=0x20090000, flags=33)
        activity = reader.agent_report(base)['pipeline_activity']
        self.assertEqual(activity['accepted_core_observations'], 8)
        self.assertEqual(activity['association_hazards']['PDS/TAG stage disagreement'], 8)

    def test_pipeline_rejects_inconsistent_flags_unknown_driver_and_abi(self):
        base = self.pipeline_fixture()
        base['diagnostics'][0]['cores'][0]['flags'] = 33
        activity = reader.agent_report(base)['pipeline_activity']
        self.assertEqual(activity['accepted_core_observations'], 7)
        self.assertEqual(activity['rejected_core_observations']['inconsistent diagnostic flags'], 1)
        base['diagnostics'][0]['abi'] = 2
        base['diagnostics'][1]['driver_fingerprint'] = 0
        activity = reader.agent_report(base)['pipeline_activity']
        self.assertFalse(activity['available'])
        self.assertEqual(activity['rejected_core_observations']['unsupported ABI/driver'], 8)

    def test_pipeline_does_not_count_failed_or_unmatched_samples_as_idle(self):
        base = self.pipeline_fixture()
        base['diagnostics'][0].update(result=-1, cores=[])
        base['sampled_fragment_timing']['signal_windows'] = []
        activity = reader.agent_report(base)['pipeline_activity']
        self.assertFalse(activity['available'])
        self.assertIsNone(activity['metrics'][0]['sample_hit_pct'])
        self.assertEqual(activity['rejected_core_observations']['outside matched PDS draw windows'], 4)

    def test_pipeline_foreign_process_excluded_by_matcher(self):
        base = self.pipeline_fixture()
        for sample in base['diagnostics']:
            sample['process_id'] = 99
        base['sampled_fragment_timing'] = reader.sampled_timing(
            self.events, base['draws'], base['scenes'], base['diagnostics'], 0)
        self.assertFalse(reader.agent_report(base)['pipeline_activity']['available'])

    def test_pipeline_unknown_group_not_decoded(self):
        base = self.pipeline_fixture(group=999)
        activity = reader.agent_report(base)['pipeline_activity']
        self.assertFalse(activity['available'])
        self.assertEqual(activity['rejected_core_observations']['unsupported signal group'], 8)

    def test_pipeline_zero_and_tiny_nonzero_are_distinct(self):
        from unittest.mock import patch
        base = self.pipeline_fixture()
        result = reader.agent_report(base)
        m = self.metric(result, 'usse_running')
        m.update(sample_hit_pct=.04, asserted=1, observed=2500)
        with patch.object(reader, 'agent_report', return_value=result):
            text = reader.format_report(base)
        self.assertIn('<0.1%', text)
        self.assertIn('1/2,500', text)
        self.assertIn('▏', text)

    def test_capture_span_excludes_untimestamped_seed(self):
        self.event(kind=24, before_us=0, after_us=0, args=(365,7,0,0))
        self.event(kind=17, call=9, before_us=1000000, after_us=1000100)
        self.assertEqual(reader.summarize(self.events)['capture_span_us'],100)

    def test_agent_report_rankings_and_coverage(self):
        import json
        base,samples = self.timing_fixture()
        base['sampled_fragment_timing'] = reader.sampled_timing(
            self.events,base['draws'],base['scenes'],samples,0)
        result = reader.agent_report(base)
        self.assertEqual(result['schema_version'],1)
        self.assertEqual(result['status'],'estimated')
        self.assertEqual(result['coverage']['timed_draws'],2)
        self.assertAlmostEqual(sum(p['estimated_fragment_ms'] for p in result['passes']),
                               result['summary']['attributed_fragment_ms'])
        self.assertAlmostEqual(sum(p['share_of_attributed_time_pct'] for p in result['passes']),100)
        self.assertEqual(len(result['draw_groups']),1)
        self.assertEqual(result['draw_groups'][0]['draw_count'],2)
        self.assertEqual(result['draw_groups'][0]['fragment_shader_id'],result['fragment_shaders'][0]['id'])
        self.assertNotIn('draws',result)
        self.assertNotIn('diagnostics',result)
        json.dumps(reader.json_numbers(result),allow_nan=False)

    def test_agent_report_unavailable_uses_null(self):
        self.setup_draw()
        self.event(call=4)
        self.event(call=2)
        result = reader.agent_report(reader.summarize(self.events))
        self.assertEqual(result['status'],'timing_unavailable')
        self.assertIsNone(result['summary']['attributed_fragment_ms'])
        self.assertIsNone(result['summary']['dominant_pass'])
        self.assertEqual(result['coverage']['untimed_draws'],1)
        self.assertIsNone(result['draw_groups'][0]['estimated_fragment_ms'])
        self.assertIsNone(result['draw_groups'][0]['rank'])

    def test_json_cli_error_is_machine_readable(self):
        import json
        import subprocess
        import sys
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable,str(Path(reader.__file__)),'--json',
                                     str(Path(directory)/'missing.csv')],capture_output=True,text=True)
        self.assertEqual(result.returncode,2)
        self.assertEqual(result.stderr,'')
        self.assertEqual(json.loads(result.stdout)['status'],'error')

    def test_sampled_timing_rejects_missing_seed_and_loss(self):
        base,samples = self.timing_fixture()
        for events,dropped in ((self.events[1:],0),(self.events,1)):
            r=reader.sampled_timing(events,base['draws'],base['scenes'],samples,dropped)
            self.assertFalse(r['available'])

    def test_sampled_timing_rejects_bad_endpoints(self):
        import copy
        base,samples = self.timing_fixture()
        for change in ('foreign','changed','unknown batch','clock','missing','wide','idle','gap'):
            data=copy.deepcopy(samples)
            s=data[1]
            if change=='foreign': s['process_id']=9
            if change=='changed':
                for c in s['cores']: c['scheduler_after'][5]=1
            if change=='unknown batch':
                for c in s['cores']: c['pds_before']=c['pds_after']=(1<<29)|(999<<16)
            if change=='clock': s['clock_after_mhz']=222
            if change=='missing': s['sample_id']=2
            if change=='wide':
                for c in s['cores']: c['timer_after']+=100000
            if change=='idle':
                for c in s['cores']: c['pds_before']=c['pds_after']=0
            if change=='gap': s['before_us']+=10000; s['after_us']+=10000
            with self.subTest(change=change):
                r=reader.sampled_timing(self.events,base['draws'],base['scenes'],data,0)
                self.assertFalse(r['available'])

    def test_sampled_timing_rejects_flush_and_failed_end(self):
        base,samples = self.timing_fixture()
        self.events[-1]['result']=-1
        self.assertFalse(reader.sampled_timing(self.events,base['draws'],base['scenes'],samples,0)['available'])
        self.events[-1]['result']=0
        self.event(call=3)
        self.assertFalse(reader.sampled_timing(self.events,base['draws'],base['scenes'],samples,0)['available'])

    def test_sampled_timer_wrap(self):
        base,samples = self.timing_fixture()
        for s in samples:
            for c in s['cores']:
                c['timer_before']=(0xfffffff0+s['sample_id']*6920)&0xffffffff
                c['timer_after']=(c['timer_before']+20)&0xffffffff
        r=reader.sampled_timing(self.events,base['draws'],base['scenes'],samples,0)
        self.assertTrue(r['available'])
        self.assertAlmostEqual(r['passes'][0]['estimated_ms'],1,places=3)

    def test_identity_wrap_rejected(self):
        base,samples = self.timing_fixture()
        self.events[0]['arg3']=0xffffffff
        self.assertFalse(reader.sampled_timing(self.events,base['draws'],base['scenes'],samples,0)['available'])

    def test_scene_labels_and_gl_parent(self):
        self.setup_draw()
        self.event(kind=6, frame=7, label="frame")
        self.event(kind=7, call=1, label="water")
        self.event(kind=19, label="surface")
        self.event(kind=18, args=(2, 5, 200), label="water shader")
        entry = self.event(kind=20, call=4)
        self.event(call=4)
        self.event(kind=20, call=4, before_us=entry["before_us"], arg7=1)
        self.event(kind=8, scope=1)
        self.event(call=2)
        r = reader.summarize(self.events)
        self.assertFalse(r["warnings"])
        d = r["draws"][0]
        self.assertEqual((d["pass"], d["name"], d["frame"]), ("water", "surface", 7))
        self.assertEqual(d["fragment_shader"]["name"], "water shader")
        self.assertEqual(d["gl_call_sequence"], entry["sequence"])
        self.assertIsNone(d["gpu_time_us"])
        self.assertEqual(d["frame_name"],"frame")

    def test_fragment_enable_is_separate_from_binding(self):
        self.setup_draw()
        self.event(call=4)
        self.event(call=23, args=(0x200000,))
        self.event(call=24, args=(0x200000,))
        self.event(call=4)
        self.event(call=23, args=(0,))
        self.event(call=4)
        self.event(call=2)
        r = reader.summarize(self.events)
        a, b, c = r["draws"]
        self.assertIsNone(a["front_fragment_enabled"])
        self.assertFalse(b["front_fragment_enabled"])
        self.assertFalse(b["back_fragment_enabled"])
        self.assertEqual(b["fragment_shader"]["handle"], 200)
        self.assertTrue(c["front_fragment_enabled"])
        self.assertFalse(r["warnings"])

    def gl_call(self, call, handle):
        entry=self.event(kind=20,call=call,args=(handle,))
        self.event(kind=20,call=call,args=(handle,),arg7=1,before_us=entry["before_us"])

    def test_gl_names_are_hints_not_gxm_shader_identity(self):
        self.setup_draw()
        self.gl_call(1,42)
        self.event(kind=18,args=(3,0,42),label="Water GL program")
        self.gl_call(3,42)
        self.event(call=4)
        self.gl_call(2,42)
        self.gl_call(1,42)  # Reused GL handle must not inherit old name.
        self.gl_call(3,42)
        self.event(call=4)
        d=reader.summarize(self.events)["draws"]
        self.assertEqual(d[0]["gl_program_hint"]["name"],"Water GL program")
        self.assertFalse(d[0]["gl_program_hint"]["binding_verified"])
        self.assertEqual(d[0]["fragment_shader"]["name"],"")
        self.assertEqual(d[1]["gl_program_hint"]["name"],"")
        self.assertEqual(d[1]["gl_program_hint"]["generation"],2)

    def test_cached_shader_references_and_address_reuse(self):
        self.setup_draw()
        self.event(call=12, context=0, args=(5, 20, 200))
        self.event(call=16, context=0, args=(5, 200))
        self.event(call=4)
        self.event(call=16, context=0, args=(5, 200))
        self.event(call=12, context=0, args=(5, 20, 200))
        self.event(call=4)  # Old binding must not attach to new lifetime.
        self.event(call=8, args=(200,))
        self.event(call=5)
        draws = reader.summarize(self.events)["draws"]
        self.assertEqual(draws[0]["fragment_shader"]["generation"], 1)
        self.assertIsNone(draws[1]["fragment_shader"])
        self.assertEqual(draws[2]["fragment_shader"]["generation"], 2)

    def test_scene_boundaries_and_flushes(self):
        self.event(call=9)
        self.event(call=1, args=(7,100,101,102,103,104,105))
        self.event(call=4)
        self.event(call=3)
        self.event(call=5)
        self.event(call=2, args=(200,201), result=-1)
        self.event(call=4)
        self.event(call=2, args=(202,203))
        self.event(call=1)
        self.event(call=2)  # Empty scene is still a scene.
        self.event(call=1)  # Unfinished scene must be visible.
        r = reader.summarize(self.events)
        a,b,c = r["scenes"]
        self.assertEqual(a["draw_sequences"], [2,4,6])
        self.assertEqual(a["flush_sequences"], [3])
        self.assertEqual(a["end_sequence"],7)
        self.assertEqual(a["fragment_notification"],203)
        self.assertEqual(a["render_target"],100)
        self.assertTrue(a["closed"] and b["closed"])
        self.assertEqual(b["draw_sequences"],[])
        self.assertFalse(c["closed"])
        self.assertIn("scene lacks observed successful end",r["warnings"])

    def test_scene_identity_survives_context_address_reuse(self):
        self.event(call=9)
        self.event(call=1)
        self.event(call=4)
        self.event(call=2)
        self.event(call=10)
        self.event(call=9)
        self.event(call=1, thread=2)
        self.event(call=4, thread=2)
        self.event(call=2, thread=2)
        report = reader.summarize(self.events)
        a,b = report["scenes"]
        self.assertEqual(a["scene"], b["scene"])
        self.assertNotEqual(a["context_generation"], b["context_generation"])
        self.assertEqual(b["begin_thread"],2)
        for scene,draw in zip(report["scenes"],report["draws"]):
            self.assertEqual(scene["context_generation"],draw["context_generation"])
            self.assertEqual(scene["draw_sequences"],[draw["sequence"]])

    def test_failed_release_does_not_retire(self):
        self.setup_draw()
        self.event(call=16, context=0, args=(5, 200), result=-1)
        self.event(call=4)
        self.assertEqual(reader.summarize(self.events)["draws"][0]["fragment_shader"]["generation"], 1)

    def test_precomputed_never_inherits_immediate_binding(self):
        self.setup_draw()
        self.event(call=6)
        d = reader.summarize(self.events)["draws"][0]
        self.assertIsNone(d["vertex_shader"])
        self.assertIsNone(d["fragment_shader"])

    def test_patcher_destroy_invalidates_programs(self):
        self.setup_draw()
        self.event(call=17, context=0, args=(5,))
        self.event(call=4)
        d = reader.summarize(self.events)["draws"][0]
        self.assertIsNone(d["vertex_shader"])
        self.assertIsNone(d["fragment_shader"])

    def test_missing_history_keeps_unknown_draw(self):
        self.event(call=4)
        report = reader.summarize(self.events)
        self.assertEqual(len(report["draws"]), 1)
        self.assertTrue(report["draws"][0]["identity_uncertain"])
        self.assertIsNone(report["draws"][0]["context_generation"])

    def test_overlap_and_overflow_are_visible(self):
        self.setup_draw()
        self.event(call=4, before_us=1)
        r = reader.summarize(self.events, dropped=4)
        self.assertTrue(r["warnings"])
        self.assertTrue(r["draws"][0]["identity_uncertain"])

    def test_late_callback_invalidates_earlier_recorded_draw(self):
        self.setup_draw()
        # The draw callback records first; a concurrently executing bind
        # records second. Both brackets must be considered before attribution.
        self.event(call=4, before_us=100, after_us=120)
        self.event(call=8, args=(200,), thread=2, before_us=110, after_us=130)
        self.event(call=2, before_us=140, after_us=150)
        d = reader.summarize(self.events)["draws"][0]
        self.assertTrue(d["identity_uncertain"])
        self.assertIsNone(d["fragment_shader"])

    def test_parallel_separate_contexts_are_not_ambiguous(self):
        self.setup_draw()
        self.event(call=9, context=2)
        self.event(call=1, context=2)
        self.event(call=4, before_us=100, after_us=120)
        self.event(call=4, context=2, thread=2, before_us=110, after_us=130)
        self.event(call=2, before_us=140, after_us=150)
        self.event(call=2, context=2, before_us=151, after_us=152)
        self.assertFalse(any(d["identity_uncertain"] for d in reader.summarize(self.events)["draws"]))

    def test_threads_have_separate_labels(self):
        self.setup_draw()
        self.event(kind=19, thread=2, label="wrong thread")
        self.event(call=4)
        self.assertEqual(reader.summarize(self.events)["draws"][0]["name"], "")

    def test_csv_round_trip_and_footer(self):
        self.event(kind=19, label='water, "reflection"\npass')
        s = io.StringIO(newline="")
        w = csv.writer(s)
        w.writerow(["gpuprof-capture", 1])
        w.writerow(["columns", *reader.COLUMNS])
        w.writerow(["event", *(self.events[0][k] for k in reader.COLUMNS)])
        incomplete = s.getvalue()
        w.writerow(["end", 1, 0])
        parsed, dropped = reader.read_capture(io.StringIO(s.getvalue()))
        self.assertEqual(parsed, self.events)
        self.assertEqual(dropped, 0)
        with self.assertRaises(ValueError):
            reader.read_capture(io.StringIO(incomplete))
        with self.assertRaises(ValueError):
            reader.read_capture(io.StringIO(s.getvalue() + "end,1,0\n"))

    def diagnostic(self):
        self.event(kind=21, call=7, before_us=10, after_us=20, args=(43,70,111,111,5))
        for core in range(4):
            for part in range(3):
                self.event(kind=22, call=7, context=core, scope=part,
                           before_us=10, after_us=20, args=tuple(range(8)))

    def test_diagnostic_round_trip(self):
        self.diagnostic()
        r = reader.summarize(self.events)
        s = r["diagnostics"][0]
        self.assertEqual(len(s["cores"]),4)
        self.assertEqual(s["cores"][2]["timer_after"],7)
        self.assertEqual(s["cores"][2]["pds_before"],6)
        self.assertFalse(r["gpu_timing_available"])

    def test_late_capture_snapshot(self):
        self.event(kind=23, call=0, context=0)
        self.event(kind=23, call=1, context=1, args=(0,10,0,11,12,2,1))
        self.event(kind=23, call=2, context=100, args=(5,11,1))
        self.event(kind=23, call=3, context=200, args=(5,12,2))
        self.event(call=1)
        self.event(call=4)
        d=reader.summarize(self.events)["draws"][0]
        self.assertEqual(d["context_generation"],10)
        self.assertEqual(d["vertex_shader"]["generation"],11)
        self.assertEqual(d["fragment_shader"]["generation"],12)
        self.assertFalse(d["front_fragment_enabled"])
        self.assertTrue(d["back_fragment_enabled"])

    def test_snapshot_after_submission_rejected(self):
        self.event(call=9)
        self.event(kind=23)
        with self.assertRaises(ValueError):
            reader.summarize(self.events)

    def test_partial_diagnostic_rejected(self):
        self.diagnostic()
        self.events.pop()
        with self.assertRaises(ValueError):
            reader.summarize(self.events)

    def test_failed_diagnostic_has_no_payload(self):
        self.event(kind=21, call=7, result=-5)
        self.assertEqual(reader.summarize(self.events)["diagnostics"][0]["cores"],[])

    def test_transfers_preserved_without_render_context(self):
        self.event(call=19,context=0,args=(16,32,100,200))
        self.event(call=20,context=0,result=-1)
        self.event(call=18,context=0)
        r=reader.summarize(self.events)
        self.assertEqual(len(r["transfers"]),2)
        self.assertEqual(len(r["presentations"]),1)
        self.assertIsNone(r["transfers"][0]["gpu_time_us"])
        self.assertEqual(r["transfers"][1]["result"],-1)
        self.assertNotIn("GXM context missing creation history",r["warnings"])


if __name__ == "__main__":
    unittest.main()
