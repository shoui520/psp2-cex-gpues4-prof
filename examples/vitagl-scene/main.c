/* Real 3D workload using only the public capture API. No GPU Lab dependency. */
#include <vitaGL.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>
#include <psp2_gpuprof_capture_diagnostic.h>
#include <psp2_gpuprof_gxm_observer.h>
#include <psp2_gpuprof_labels.h>
#include <psp2_gpuprof_vitagl.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

unsigned int _newlib_heap_size_user=48*1024*1024;
enum { CAPACITY=131072, SLICES=32, STACKS=24, FRAMES=30 };
static Psp2GpuProfCaptureEvent events[CAPACITY];
static Psp2GpuProfCapture capture;
static Psp2GpuProfLabels labels;
static Psp2GpuProfInfo info;
static SceUID mutex;
static atomic_uint stop_sampler;
static unsigned samples, sample_failures;
static uint64_t read_failures;
static FILE *logfile;
static void lock(void *p) { (void)p; if(sceKernelLockMutex(mutex,1,NULL)<0) abort(); }
static void unlock(void *p) { (void)p; if(sceKernelUnlockMutex(mutex,1)<0) abort(); }
static void log_status(const char *stage,int rc) {
    if(logfile) { fprintf(logfile,"%s,%d\n",stage,rc); fflush(logfile); }
}
static int sample_thread(SceSize size,void *arg) {
    (void)size; (void)arg;
    static const unsigned groups[]={2,4,43,55,71,72,75,76,77,78,103,104,107,108,109,110};
    uint64_t id=0;
    while(!atomic_load(&stop_sampler)) {
        unsigned group=groups[id%16];
        Psp2GpuProfDiagnosticConfig cfg={.size=sizeof(cfg),.abi=PSP2_GPUPROF_ABI,
            .group=group,.tag_group=group>=103 ? 102 : 70};
        Psp2GpuProfDiagnostic data={0};
        Psp2GpuProfDiagnosticObservation o={.sampler_thread=(uint32_t)sceKernelGetThreadId(),
            .sample_id=id++,.group=group,.tag_group=cfg.tag_group,
            .process_id=(uint32_t)sceKernelGetProcessId(),.driver_fingerprint=info.driver_fingerprint};
        int clock=scePowerGetGpuClockFrequency();
        o.clock_before_mhz=clock>0 ? (unsigned)clock : 0;
        o.before_us=sceKernelGetSystemTimeWide();
        o.result=psp2GpuProfReadDiagnostic(&cfg,&data);
        o.after_us=sceKernelGetSystemTimeWide();
        clock=scePowerGetGpuClockFrequency();
        o.clock_after_mhz=clock>0 ? (unsigned)clock : 0;
        if(o.result) ++read_failures;
        int rc=psp2GpuProfCaptureDiagnostic(&capture,&o,o.result ? NULL : &data);
        if(rc) ++sample_failures; else ++samples;
        sceKernelDelayThread(500);
    }
    return 0;
}

static const char *vs="app0:shaders/scene_v.gxp",*fs="app0:shaders/scene_f.gxp";
static const char *post_vs="app0:shaders/post_v.gxp",*post_fs="app0:shaders/post_f.gxp";
/* Keep loaded GXP storage alive for the entire process: native shader ownership
 * must not depend on whether a particular vitaGL build copies this buffer. */
static GLuint shader(GLenum type,const char *path) {
    log_status(path,0);
    FILE *file=fopen(path,"rb");if(!file)return 0;
    if(fseek(file,0,SEEK_END)) {fclose(file);return 0;}
    long length=ftell(file);
    if(length<=0||length>1024*1024||fseek(file,0,SEEK_SET)){fclose(file);return 0;}
    void *binary=malloc((size_t)length);
    if(!binary){fclose(file);return 0;}
    if(fread(binary,1,(size_t)length,file)!=(size_t)length){free(binary);fclose(file);return 0;}
    fclose(file);
    GLuint s=glCreateShader(type); vglShaderGxpBinary(1,&s,binary,(GLsizei)length);
    GLint ok=0;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok) { char text[2048]; glGetShaderInfoLog(s,sizeof(text),NULL,text);
        if(logfile) {fprintf(logfile,"shader error: %s\n",text);fflush(logfile);} return 0; }
    return s;
}
static GLuint program(const char *vertex,const char *fragment,const char *name) {
    GLuint v=shader(GL_VERTEX_SHADER,vertex),f=shader(GL_FRAGMENT_SHADER,fragment);
    if(!v || !f) return 0;
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);
    glBindAttribLocation(p,0,"position");glBindAttribLocation(p,1,"normal");glBindAttribLocation(p,2,"uv");
    log_status("before native program link",(int)p);
    glLinkProgram(p); GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);
    log_status("native program link status",ok);
    if(!ok) {char text[2048];glGetProgramInfoLog(p,sizeof(text),NULL,text);
        if(logfile) {fprintf(logfile,"link error: %s\n",text);fflush(logfile);} return 0;}
    glDeleteShader(v);glDeleteShader(f);
    psp2GpuProfShaderName(&labels,sceKernelGetSystemTimeWide(),3,0,p,name);
    return p;
}
typedef struct Vertex { float p[3],n[3],uv[2]; } Vertex;
static Vertex sphere[(SLICES+1)*(STACKS+1)];
static GLushort indices[SLICES*STACKS*6];
static void mesh(void) {
    for(unsigned y=0;y<=STACKS;++y) for(unsigned x=0;x<=SLICES;++x) {
        float a=(float)x/SLICES*6.2831853f,b=(float)y/STACKS*3.14159265f;
        Vertex *v=&sphere[y*(SLICES+1)+x];
        v->p[0]=v->n[0]=sinf(b)*cosf(a);v->p[1]=v->n[1]=cosf(b);v->p[2]=v->n[2]=sinf(b)*sinf(a);
        v->uv[0]=(float)x/SLICES;v->uv[1]=(float)y/STACKS;
    }
    unsigned n=0;
    for(unsigned y=0;y<STACKS;++y) for(unsigned x=0;x<SLICES;++x) {
        unsigned a=y*(SLICES+1)+x,b=a+SLICES+1;
        indices[n++]=a;indices[n++]=b;indices[n++]=a+1;
        indices[n++]=a+1;indices[n++]=b;indices[n++]=b+1;
    }
}
static void attributes(const Vertex *v) {
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),v->p);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),v->n);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),v->uv);
}
static void name_draw(const char *name) {psp2GpuProfDrawName(&labels,sceKernelGetSystemTimeWide(),name);}
static int pass(const char *name) {return psp2GpuProfScopePush(&labels,sceKernelGetSystemTimeWide(),name);}
static void endpass(int rc) {name_draw(NULL);if(!rc) psp2GpuProfScopePop(&labels,sceKernelGetSystemTimeWide());}

int main(void) {
    sceIoMkdir("ux0:data/gpuprof-3d",0777);
    logfile=fopen("ux0:data/gpuprof-3d/status.txt","w");log_status("starting",0);
    mutex=sceKernelCreateMutex("capture-3d",0,0,NULL);
    if(mutex<0) {log_status("mutex",mutex);sceKernelExitProcess(1);}
    Psp2GpuProfCaptureConfig cfg={events,CAPACITY,lock,unlock,NULL};
    int rc=psp2GpuProfCaptureInit(&capture,&cfg);if(!rc)rc=psp2GpuProfCaptureStart(&capture);
    if(rc) {log_status("capture init",rc);sceKernelExitProcess(1);}
    /* This example owns a fresh GXM lifetime on the tested retail 3.65 setup.
       Never use this zero seed for an already-initialized engine. */
    rc=psp2GpuProfCaptureGxmIdentitySeed365(&capture,(uint32_t)sceKernelGetProcessId(),0,0);
    if(rc) {log_status("identity seed",rc);sceKernelExitProcess(1);}
    psp2GpuProfGxmSetObserver(psp2GpuProfCaptureGxmCall,&capture);psp2GpuProfVitaGLCapture(&capture);
    psp2GpuProfLabelsBegin(&labels,&capture,(uint32_t)sceKernelGetThreadId());
    log_status("vglInit resolution flag",vglInitExtended(1024*1024,960,544,16*1024*1024,SCE_GXM_MULTISAMPLE_NONE));
    GLuint scene=program(vs,fs,"Textured two-light specular"),post=program(post_vs,post_fs,"Five-tap composite");
    if(!scene||!post) {log_status("program setup failed",-1);sceKernelExitProcess(1);}
    mesh();
    GLuint tex,color,depth,fbo;
    static unsigned char pixels[256*256*4];
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){unsigned i=(y*256+x)*4;int c=((x/16)^(y/16))&1;
        pixels[i]=c?230:35;pixels[i+1]=c?170:85;pixels[i+2]=c?65:180;pixels[i+3]=255;}
    glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,256,256,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,960,544,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT16,960,544);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    GLenum fb=glCheckFramebufferStatus(GL_FRAMEBUFFER);log_status("framebuffer status",fb);
    if(fb!=GL_FRAMEBUFFER_COMPLETE)sceKernelExitProcess(1);
    info.size=sizeof(info);info.abi=PSP2_GPUPROF_ABI;rc=psp2GpuProfGetInfo(&info);log_status("GetInfo",rc);
    log_status("plugin status",info.status);log_status("capabilities",info.capabilities);
    SceUID thread=-1;
    if(!rc&&!info.status&&(info.capabilities&PSP2_GPUPROF_CAP_DIAGNOSTIC)) {
        thread=sceKernelCreateThread("gpu-sampler",sample_thread,0x10000100,0x10000,0,0,NULL);
        log_status("sampler create",thread);
        if(thread>=0) {rc=sceKernelStartThread(thread,0,NULL);log_status("sampler start",rc);if(rc<0){sceKernelDeleteThread(thread);thread=-1;}}
    }
    GLint center=glGetUniformLocation(scene,"center"),scale=glGetUniformLocation(scene,"scale"),angle=glGetUniformLocation(scene,"angle"),alpha=glGetUniformLocation(scene,"alpha");
    static const Vertex quad[]={{{-1,-1,0},{0,0,1},{0,0}},{{1,-1,0},{0,0,1},{1,0}},{{-1,1,0},{0,0,1},{0,1}},{{1,1,0},{0,0,1},{1,1}}};
    glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);glEnableVertexAttribArray(2);
    glActiveTexture(GL_TEXTURE0);
    for(unsigned frame=0;frame<FRAMES;++frame) {
        psp2GpuProfFrameName(&labels,sceKernelGetSystemTimeWide(),frame,"3D scene");
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,960,544);
        int scope=pass("Opaque textured geometry");name_draw("Color and depth clear");
        glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
        glClearColor(0.025f,0.04f,0.075f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glUseProgram(scene);glBindTexture(GL_TEXTURE_2D,tex);glUniform1i(glGetUniformLocation(scene,"tex"),0);attributes(sphere);
        glUniform1f(alpha,1);glUniform1f(angle,frame*0.025f);
        for(unsigned i=0;i<24;++i){char name[48];snprintf(name,sizeof(name),"Lit sphere %u",i);name_draw(name);
            glUniform3f(center,((int)(i%6)-2.5f)*2.1f,((int)(i/6)-1.5f)*2.0f,sinf(i*1.7f+frame*0.03f)*1.8f);
            glUniform3f(scale,0.94f,0.94f,0.94f);glDrawElements(GL_TRIANGLES,SLICES*STACKS*6,GL_UNSIGNED_SHORT,indices);}
        endpass(scope);scope=pass("Transparent overdraw");glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
        glUniform1f(alpha,0.13f);
        for(unsigned i=0;i<8;++i){char name[48];snprintf(name,sizeof(name),"Transparent shell %u",i);name_draw(name);
            glUniform3f(center,sinf(frame*0.02f)*1.5f,0,2.0f+i*0.2f);glUniform3f(scale,3.5f,3.0f,0.5f);
            glDrawElements(GL_TRIANGLES,SLICES*STACKS*6,GL_UNSIGNED_SHORT,indices);}
        endpass(scope);scope=pass("Offscreen composite");name_draw("Five-tap filtered fullscreen");
        glBindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_BLEND);glDisable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
        glUseProgram(post);glBindTexture(GL_TEXTURE_2D,color);glUniform1i(glGetUniformLocation(post,"tex"),0);attributes(quad);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);endpass(scope);vglSwapBuffers(GL_FALSE);
    }
    atomic_store(&stop_sampler,1);
    if(thread>=0){sceKernelWaitThreadEnd(thread,NULL,NULL);sceKernelDeleteThread(thread);}
    log_status("GL error after rendering",glGetError());
    rc=psp2GpuProfCaptureStop(&capture);
    FILE *file=fopen("ux0:data/gpuprof-3d/capture.csv","w");if(!file)rc=PSP2_GPUPROF_CAPTURE_IO;
    if(!rc)rc=psp2GpuProfCaptureWriteCsv(&capture,file);
    if(file&&fclose(file))rc=PSP2_GPUPROF_CAPTURE_IO;
    log_status("export",rc);log_status("samples recorded",samples);log_status("sample record failures",sample_failures);
    if(logfile){fprintf(logfile,"read failures,%" PRIu64 "\nevents,%" PRIu64 "\ndropped,%" PRIu64 "\n",read_failures,(uint64_t)capture.count,capture.dropped);fclose(logfile);}
    /* Other vitaGL workers may still call GXM. Keep sealed recorder and mutex
       alive through process exit; never replace notifications or add finishes. */
    sceKernelExitProcess(rc?1:0);return 0;
}
