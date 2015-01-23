//  Natron
//
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * Created by Alexandre GAUTHIER-FOICHAT on 6/1/2012.
 * contact: immarespond at gmail dot com
 *
 */


#ifndef GUIAPPWRAPPER_H
#define GUIAPPWRAPPER_H

#include "Engine/AppInstanceWrapper.h"
#include "Gui/GuiAppInstance.h"

class GuiApp : public App
{
    GuiAppInstance* _app;
    
public:
    
    GuiApp(AppInstance* app);
    
    virtual ~GuiApp();
};

#endif // GUIAPPWRAPPER_H