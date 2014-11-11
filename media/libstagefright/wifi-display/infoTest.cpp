
#include <utils/Log.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/ISurfaceComposer.h>
#include <ui/DisplayInfo.h>
int main(int argc, char **argv) {
android::DisplayInfo info;
android::SurfaceComposerClient::getDisplayInfo(android::SurfaceComposerClient::getBuiltInDisplay(
            android::ISurfaceComposer::eDisplayIdMain), &info);
    fprintf(stdout, "w=%d, h=%d\n", info.w, info.h); 
return 0;
}
