
/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "AmloadAmlogicPlayers"
#include <utils/Log.h>
#include <cutils/properties.h>
#include <utils/threads.h>
#include <utils/KeyedVector.h>

#include "AmSupportModules_priv.h"

namespace android
{

static sp<AmSharedLibrary> gLibAmlThumbNail;
bool  LoadAndInitAmlogicMetadataRetrieverFactory(void)
{
    int err;
    String8 name("libamlogic_metadata_retriever.so");
    gLibAmlThumbNail = new AmSharedLibrary(name);
    if (!*gLibAmlThumbNail) {
        ALOGE("load libamlogic_metadata_retriever.so for AmlogicMetadataRetriever failed:%s", gLibAmlThumbNail->lastError());
        gLibAmlThumbNail.clear();
        return false;
    }
    typedef int (*init_fun)(void);

    init_fun init = (init_fun)gLibAmlThumbNail->lookup("_ZN7android35AmlogicMetadataRetrieverFactoryInitEv");

    if (init == NULL) {
        ALOGE("AmlogicMetadataRetrieverFactoryInit failed:%s", gLibAmlThumbNail->lastError());
        return false;
    }
    err = init();
    if (err != 0) {
        ALOGE("AmlogicMetadataRetrieverFactoryInit failed:%s", gLibAmlThumbNail->lastError());
        return false;
    }

    return true;
}


}



