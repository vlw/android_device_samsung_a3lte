/* SPDX-License-Identifier: Apache-2.0 */
// Explicit unwrapped HAL Bayer capture diagnostic. No production camera changes.
#include <hardware/camera.h>
#include <hardware/hardware.h>
#include <camera/CameraParameters.h>
#include <utils/String8.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>
struct Memory { camera_memory_t base; };
static camera_memory_t* memory(int fd,size_t size,unsigned count,void*) {
    auto* m = new Memory{};
    m->base.size = size * count;
    m->base.data = mmap(nullptr,m->base.size,PROT_READ|PROT_WRITE,
            fd < 0 ? MAP_PRIVATE|MAP_ANONYMOUS : MAP_SHARED, fd, 0);
    if(m->base.data==MAP_FAILED){delete m;return nullptr;}
    m->base.release=[](camera_memory_t* b){munmap(b->data,b->size);delete reinterpret_cast<Memory*>(b);};
    return &m->base;
}
struct State { std::mutex mutex; std::condition_variable changed; int count=0;bool error=false;size_t pixels=0;std::string prefix; };
static void notify(int32_t type,int32_t a,int32_t b,void* user){
    auto* s=static_cast<State*>(user);std::lock_guard<std::mutex> lock(s->mutex);
    printf("notify type=%x ext=%d,%d\n",type,a,b);fflush(stdout);
    if(type==CAMERA_MSG_ERROR){s->error=true;s->changed.notify_all();}
}
static void rawData(int32_t type,const camera_memory_t* mem,unsigned index,camera_frame_metadata_t*,void* user){
    auto* s=static_cast<State*>(user);std::lock_guard<std::mutex> lock(s->mutex);
    printf("callback type=%x bytes=%zu index=%u\n",type,mem?mem->size:0,index);fflush(stdout);
    if(type!=CAMERA_MSG_RAW_IMAGE && type!=CAMERA_MSG_COMPRESSED_IMAGE)return;
    if(!mem || !mem->data || index || mem->size < s->pixels || s->count){return;}
    std::string path=s->prefix+".raw";
    FILE* f=fopen(path.c_str(),"wb");
    if(!f || fwrite(mem->data,1,mem->size,f)!=mem->size)s->error=true;
    if(f)fclose(f);
    ++s->count;fflush(stdout);s->changed.notify_all();
}

int main(int argc,char** argv){
    if(argc!=4){fprintf(stderr,"usage: a3_raw_capture_test CAMERA_ID PICTURE_FORMAT OUTPUT_PREFIX\n");return 2;}
    const hw_module_t* module=nullptr;camera_device_t* camera=nullptr;
    int rc=hw_get_module_by_class("camera","vendor",&module);
    if(rc)return 3;
    camera_module_callbacks_t callbacks{};
    callbacks.camera_device_status_change=[](const camera_module_callbacks_t*,int,int){};
    callbacks.torch_mode_status_change=[](const camera_module_callbacks_t*,const char*,int){};
    auto* cameraModule=reinterpret_cast<const camera_module_t*>(module);
    if(cameraModule->set_callbacks)cameraModule->set_callbacks(&callbacks);
    rc=module->methods->open(module,argv[1],reinterpret_cast<hw_device_t**>(&camera));
    if(rc){printf("open=%d\n",rc);return 4;}
    State state;state.prefix=argv[3];
    camera->ops->set_callbacks(camera,notify,rawData,nullptr,memory,&state);
    camera->ops->enable_msg_type(camera,CAMERA_MSG_ERROR|CAMERA_MSG_SHUTTER|CAMERA_MSG_RAW_IMAGE|CAMERA_MSG_RAW_IMAGE_NOTIFY|CAMERA_MSG_COMPRESSED_IMAGE);
    char* raw=camera->ops->get_parameters(camera);
    android::CameraParameters p{android::String8(raw)};
    camera->ops->put_parameters(camera,raw);
    int width,height;p.getPictureSize(&width,&height);
    state.pixels=size_t(width)*height;
    p.setPreviewSize(640,480);p.set("no-display-mode",1);p.set("recording-hint","false");
    p.set("scene-mode","auto");p.set("flash-mode","off");p.set("zsl","off");
    p.set("picture-format",argv[2]);p.set("ae-bracket-hdr","Off");
    p.set("num-snaps-per-shutter",1);p.set("jpeg-thumbnail-width",0);p.set("jpeg-thumbnail-height",0);
    rc=camera->ops->set_parameters(camera,p.flatten().string());printf("set parameters=%d\n",rc);
    if(!rc){rc=camera->ops->start_preview(camera);printf("start preview=%d\n",rc);}
    if(!rc){
        sleep(3);raw=camera->ops->get_parameters(camera);printf("active parameters=%s\n",raw);camera->ops->put_parameters(camera,raw);
        rc=camera->ops->take_picture(camera);printf("take picture=%d\n",rc);fflush(stdout);
        if(!rc){std::unique_lock<std::mutex> lock(state.mutex);state.changed.wait_for(lock,std::chrono::seconds(20),[&]{return state.count>=1||state.error;});}
    }
    camera->ops->disable_msg_type(camera,-1);
    camera->ops->cancel_picture(camera);camera->ops->stop_preview(camera);
    camera->ops->release(camera);camera->common.close(reinterpret_cast<hw_device_t*>(camera));
    printf("%s raw capture: %d buffers at nominal %dx%d; validate packing separately\n",!rc&&!state.error&&state.count==1?"PASS":"FAIL",state.count,width,height);
    return !rc&&!state.error&&state.count==1?0:1;
}
