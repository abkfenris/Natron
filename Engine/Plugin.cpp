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

#include "Plugin.h"

#include <QMutex>

#include "Engine/LibraryBinary.h"

using namespace Natron;

Plugin::~Plugin()
{
    if (_lock) {
        delete _lock;
    }
    if (_binary) {
        delete _binary;
    }
}

void
PluginGroupNode::tryAddChild(const boost::shared_ptr<PluginGroupNode>& plugin)
{
    for (std::list<boost::shared_ptr<PluginGroupNode> >::iterator it = _children.begin() ;it!=_children.end();++it) {
        if (*it == plugin) {
            return;
        }
    }
    _children.push_back(plugin);
}

void
PluginGroupNode::tryRemoveChild(PluginGroupNode* plugin)
{
    for (std::list<boost::shared_ptr<PluginGroupNode> >::iterator it = _children.begin(); it != _children.end(); ++it) {
        if (it->get() == plugin) {
            _children.erase(it);
            return;
        }
    }
}
