/* SPDX-License-Identifier: Apache-2.0 */
// Real wrapper against a fake vendor module. Never opens a sensor or recorder.
#include "../CameraWrapper.cpp"
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1); } } while (0)
using android::CameraParameters;
using android::String8;
struct Fake {
    camera_device_t device{};
    camera_device_ops_t ops{};
    CameraParameters params;
    int releases=0, closes=0, memoryCalls=0, callbacks=0;
    uint32_t messages=0;
    bool preview=false, recording=false, hdr=false;
    bool failCommand=false, failParameters=false, failPreview=false;
    camera_notify_callback notify=nullptr;
    camera_data_callback data=nullptr;
    camera_request_memory memory=nullptr;
    void* cookie=nullptr;
    Fake() {
        device.ops=&ops;device.priv=this;
        device.common.close=[](hw_device_t* d) { ++self(reinterpret_cast<camera_device_t*>(d))->closes;return 0; };
        params.unflatten(String8("scene-mode=auto;scene-mode-values=auto,night;picture-size=640x480;picture-format=jpeg;jpeg-quality=96;flash-mode=auto;zsl=on;recording-hint=false;preview-size-values=1920x1080,640x480;exposure-compensation-step=0.1;min-exposure-compensation=-20;max-exposure-compensation=20;preview-fps-range=15000,30000"));
        ops.get_parameters=[](camera_device_t* d) { return strdup(self(d)->params.flatten().string()); };
        ops.put_parameters=[](camera_device_t*,char* p) { free(p); };
        ops.set_parameters=[](camera_device_t* d,const char* p) {
            auto* f=self(d);if(f->failParameters){f->failParameters=false;return -EINVAL;}
            f->params.unflatten(String8(p));
            if(f->hdr){f->params.set("picture-format","nv21");f->params.set("capture-burst-exposures","-2,0,2");}
            return 0;
        };
        ops.send_command=[](camera_device_t* d,int32_t cmd,int32_t a,int32_t) {
            auto* f=self(d);if(cmd==1014)f->hdr=a;
            if(f->failCommand){f->failCommand=false;return -EIO;}return 0;
        };
        ops.start_preview=[](camera_device_t* d) {auto* f=self(d);if(f->failPreview){f->failPreview=false;return -EIO;}f->preview=true;return 0;};
        ops.stop_preview=[](camera_device_t* d) {self(d)->preview=false;};
        ops.preview_enabled=[](camera_device_t* d) -> int {return self(d)->preview;};
        ops.recording_enabled=[](camera_device_t* d) -> int {return self(d)->recording;};
        ops.start_recording=[](camera_device_t* d) {auto* f=self(d);CHECK(!f->hdr);f->recording=true;return 0;};
        ops.stop_recording=[](camera_device_t* d) {self(d)->recording=false;};
        ops.release=[](camera_device_t* d) {++self(d)->releases;};
        ops.enable_msg_type=[](camera_device_t* d,int32_t bits){self(d)->messages|=bits;};
        ops.disable_msg_type=[](camera_device_t* d,int32_t bits){self(d)->messages&=~bits;};
        ops.msg_type_enabled=[](camera_device_t* d,int32_t bits) -> int {return (self(d)->messages&bits)!=0;};
        ops.set_callbacks=[](camera_device_t* d,camera_notify_callback n,camera_data_callback data,camera_data_timestamp_callback,camera_request_memory m,void* u){auto* f=self(d);f->notify=n;f->data=data;f->memory=m;f->cookie=u;};
    }
    static Fake* self(camera_device_t* d){return static_cast<Fake*>(d->priv);}
};
static Fake* vendors[2];
static camera_memory_t* memory(int,size_t,uint_t,void* user){++static_cast<Fake*>(user)->memoryCalls;return nullptr;}
static void data(int32_t,const camera_memory_t*,unsigned,camera_frame_metadata_t*,void* user){++static_cast<Fake*>(user)->callbacks;}
static int change(camera_device_t* d,const char* scene,bool recording=false){
    char* raw=d->ops->get_parameters(d);CHECK(raw);CameraParameters p{String8(raw)};d->ops->put_parameters(d,raw);
    p.set("scene-mode",scene);p.set("recording-hint",recording?"true":"false");return d->ops->set_parameters(d,p.flatten().string());
}
int main(){
    Fake rear,front;vendors[0]=&rear;vendors[1]=&front;
    hw_module_methods_t methods{};
    methods.open=[](const hw_module_t*,const char* id,hw_device_t** out){*out=&vendors[atoi(id)]->device.common;return 0;};
    camera_module_t module{};module.common.methods=&methods;module.get_number_of_cameras=[](){return 2;};gVendorModule=&module;
    camera_device_t *back=nullptr,*selfie=nullptr;
    CHECK(camera_device_open(&HAL_MODULE_INFO_SYM.common,"0",reinterpret_cast<hw_device_t**>(&back))==0);
    CHECK(camera_device_open(&HAL_MODULE_INFO_SYM.common,"1",reinterpret_cast<hw_device_t**>(&selfie))==0);
    hw_device_t* duplicate=nullptr;CHECK(camera_device_open(&HAL_MODULE_INFO_SYM.common,"0",&duplicate)==-EBUSY);
    CHECK(camera_device_open(&HAL_MODULE_INFO_SYM.common,"2",&duplicate)==-EINVAL);
    CHECK(camera_start_recording(nullptr)==-EINVAL);
    back->ops->set_callbacks(back,nullptr,data,nullptr,memory,&rear);
    selfie->ops->set_callbacks(selfie,nullptr,data,nullptr,memory,&front);
    rear.memory(-1,64,1,reinterpret_cast<void*>(123));front.memory(-1,64,1,reinterpret_cast<void*>(456));
    CHECK(rear.memoryCalls==1&&front.memoryCalls==1);
    CHECK(change(selfie,"hdr")==-EINVAL);
    CHECK(back->ops->set_parameters(back,nullptr)==-EINVAL);
    CHECK(back->ops->start_preview(back)==0);
    std::string initial=rear.params.flatten().string();
    for(int failure=0;failure<3;++failure){
        rear.failCommand=failure==0;rear.failParameters=failure==1;rear.failPreview=failure==2;
        CHECK(change(back,"hdr")<0);CHECK(!rear.hdr&&rear.preview);
        CHECK(!reinterpret_cast<wrapper_camera_device_t*>(back)->hdr_selected);
        CHECK(rear.params.flatten().string()==initial);
    }
    CHECK(change(back,"hdr")==0&&rear.hdr&&rear.preview);
    char* raw=back->ops->get_parameters(back);CameraParameters p{String8(raw)};back->ops->put_parameters(back,raw);
    CHECK(!strcmp(p.get("scene-mode"),"hdr")&&!strcmp(p.get("picture-format"),"jpeg"));
    CHECK(!strcmp(p.get("preview-size-values"),"1920x1080,640x480"));
    CHECK(!strcmp(p.get("exposure-compensation-step"),"0.1"));
    // A late NV21 buffer after a capture/cancel must never reach the JPEG app callback.
    camera_memory_t frame{};rear.data(CAMERA_MSG_COMPRESSED_IMAGE,&frame,0,nullptr,rear.cookie);CHECK(rear.callbacks==0);
    CHECK(change(back,"hdr",true)==0&&!rear.hdr);
    CHECK(!strcmp(rear.params.get("picture-format"),"jpeg"));
    CHECK(change(back,"hdr")==0&&rear.hdr);
    CHECK(back->ops->start_recording(back)==0&&!rear.hdr&&rear.recording);
    CHECK(change(back,"hdr")==-EBUSY&&!rear.hdr);
    back->ops->stop_recording(back);CHECK(!rear.recording);
    CHECK(change(back,"auto")==0);
    back->ops->enable_msg_type(back,CAMERA_MSG_COMPRESSED_IMAGE|CAMERA_MSG_SHUTTER);
    CHECK(back->ops->msg_type_enabled(back,CAMERA_MSG_COMPRESSED_IMAGE));
    back->ops->disable_msg_type(back,CAMERA_MSG_COMPRESSED_IMAGE);
    CHECK(!back->ops->msg_type_enabled(back,CAMERA_MSG_COMPRESSED_IMAGE));
    back->ops->release(back);back->ops->release(back);CHECK(rear.releases==1);
    CHECK(back->common.close(&back->common)==0&&rear.releases==1&&rear.closes==1);
    // Closing the rear must not free the front's parameters or callbacks.
    raw=selfie->ops->get_parameters(selfie);CHECK(raw);selfie->ops->put_parameters(selfie,raw);
    front.memory(-1,64,1,nullptr);CHECK(front.memoryCalls==2);
    CHECK(selfie->common.close(&selfie->common)==0&&front.releases==1&&front.closes==1);
    puts("PASS wrapper: HDR/video transitions, command/parameters/preview rollback, per-camera callbacks, late NV21, masks, release/close");
}
