#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <cutils/properties.h>
//#include <gui/SurfaceTextureClient.h>
#include <gui/Surface.h>
//#include "CameraHardwareInterface.h"
#include "CameraService.h"
#include <utils/Log.h>
#include <expat.h>
#include "LoadXml.h"
namespace android { 
#define LOG1(...) ALOGD_IF(gLogLevel >= 1, __VA_ARGS__);
#define LOG2(...) ALOGD_IF(gLogLevel >= 2, __VA_ARGS__);
char* LoadXml::getApkPackageName(int callingPid){
    cameraCallProcess[0] = 0x00;   
    sprintf(cameraCallProcess,"/proc/%d/cmdline",callingPid);  
    f = open(cameraCallProcess, O_RDONLY);    
    if (f < 0) {  
        memset(cameraCallProcess,0x00,sizeof(cameraCallProcess));  
    } else {  
        memset(cameraCallProcess,0x00,sizeof(cameraCallProcess));  
        read(f, cameraCallProcess, 64);  
        close(f);	
        f= -1;  
        LOG1("Calling process is: %s",cameraCallProcess);  
    } 
    return cameraCallProcess;
}
int LoadXml::destroyList_Open(List_Open *head){ 
    List_Open  *p; 
    if(head==NULL) 
        return 0; 
    while(head){
        p=head->next; 
        delete head; 
        head=NULL;
        head=p; 
    } 
    return 1; 
}
int LoadXml::destroyList_Or(List_Or *head){    
    List_Or *p;    
    if(head==NULL)    
        return 0;    
    while(head){  
        p=head->next;   
        delete head;
        head=NULL;
        head=p;    
    }
    return 1;
 }
 
List_Or::~List_Or(){
    delete apk_name;
    apk_name=NULL;
    delete pro;
    pro=NULL;
    delete capo;
    capo=NULL;
    
}
List_Open::~List_Open(){
    delete apk_name;
    apk_name=NULL;
    delete camera_number;
    camera_number=NULL;
        
}
 
LoadXml::~LoadXml(){
    destroyList_Open(Lo_Head);
    destroyList_Or(Lr_Head);
}
List_Open::List_Open(char const* apk,char const*camera){  
    apk_name=new char[strlen(apk)+1];
    strcpy(apk_name, apk);
    camera_number=new char[strlen(camera)+1];
    strcpy(camera_number, camera);
}
List_Or::List_Or(char const* apk,char const* pr,char const* cap){
    apk_name=new char[strlen(apk)+1];
    strcpy(apk_name, apk);
    pro=new char[strlen(pr)+1];
    strcpy(pro, pr);
    capo=new char[strlen(cap)+1];
    strcpy(capo, cap);
}
List_Or::List_Or(){
    apk_name=NULL;
    pro=NULL;
    capo=NULL;
}
LoadXml::LoadXml(){
    Lo_Head=NULL;
    Lr_Head=NULL;
    indent=0;
}
bool LoadXml::findApkOp(char * apk_name){  
    List_Open* Lo_temp=Lo_Head;
    while(Lo_temp!=NULL){
        if(!strcmp(Lo_temp->apk_name,apk_name)){
            return true;
        }
        Lo_temp=Lo_temp->next;
    }
    return false;
}
int LoadXml::findApkCp(char * apk_name,List_Or* temp){
    List_Or* Lr_temp=Lr_Head;
    while(Lr_temp!=NULL){
        if(!strcmp(Lr_temp->apk_name,apk_name)) {
            temp->apk_name=new char[strlen(Lr_temp->apk_name)+1];
            strcpy(temp->apk_name, Lr_temp->apk_name);
            temp->pro=new char[strlen(Lr_temp->pro)+1];
            strcpy(temp->pro, Lr_temp->pro);
            temp->capo=new char[strlen(Lr_temp->capo)+1];
            strcpy(temp->capo, Lr_temp->capo);
            return 1;
        }	
        Lr_temp=Lr_temp->next;
    }
    return 0;
}
void LoadXml::print(){
    List_Open* Lo_temp=Lo_Head;
    List_Or*   Lr_temp=Lr_Head;
    while(Lo_temp!=NULL){
        LOG1("apk_Name--------%s",Lo_temp->apk_name);
        LOG1("camera_number--------%s",Lo_temp->camera_number);
        Lo_temp=Lo_temp->next;
    }
    while(Lr_temp!=NULL){
        LOG1("apk_Name--------%s",Lr_temp->apk_name);
        LOG1("capo--------%s",Lr_temp->capo);
        LOG1("pro--------%s",Lr_temp->pro);
        Lr_temp=Lr_temp->next;
    } 
}
// static
void LoadXml::StartElementHandlerWrapper(void *data ,const char *name, const char **attrs){     
    static_cast<LoadXml *>(data)->startElementHandler(name, attrs); 
}  
// static
void LoadXml::EndElementHandlerWrapper(void *me, const char *name){
    static_cast<LoadXml *>(me)->endElementHandler(name);
}
void LoadXml::startElementHandler(const char *name, const char **attrs){
    int i;
    int j;
    List_Open* q=NULL;
    List_Or*   l=NULL;
    if(attrs[4]==NULL){
        for (i=0;attrs[i];i+=4) {
            q=new List_Open(attrs[i+1],attrs[i+3]);    
            q->next=Lo_Head;                         
            Lo_Head=q;
        }
    }else{
        for (j=0;attrs[j];j+=6) {
            l=new List_Or(attrs[j+1],attrs[j+3],attrs[j+5]);  
            l->next=Lr_Head;              
            Lr_Head=l;
        }
    }
    ++indent;
}
void LoadXml::endElementHandler(const char *name){
    --indent;
}
void LoadXml::parseXMLFile(){
    const int BUFF_SIZE = 1024;
    char* filename="/etc/Third_party_apk_camera.xml"; 
    if ((fp=fopen(filename, "r"))==NULL){  
        return ; 
    } 
    if (NULL == fp){
        LOG1("failed to open demo.xml!/n");
        return;
    }
    XML_Parser parser = ::XML_ParserCreate(NULL);
    ::XML_SetUserData(parser, this);
    ::XML_SetElementHandler(parser, StartElementHandlerWrapper, EndElementHandlerWrapper);
    void *buff = ::XML_GetBuffer(parser, BUFF_SIZE);
    if (buff == NULL){
        LOG1("failed to in call to XML_GetBuffer()");
    }
    int bytes_read = ::fread(buff, 1, BUFF_SIZE, fp);
    if (bytes_read < 0) {
        LOG1("failed in call to read");
    }
    if (::XML_ParseBuffer(parser, bytes_read, bytes_read == 0)!= XML_STATUS_OK){
        LOG1("failed parser xml");
    }
    ::XML_ParserFree(parser);
    fclose(fp);
    fp=NULL;
}
}

