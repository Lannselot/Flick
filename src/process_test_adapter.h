// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

class FlickApplication;
class TestPlatformServices;
class ViewerWindowTestControl;

void installProcessTestAdapter(ViewerWindowTestControl &window, FlickApplication &application,
                               TestPlatformServices &platformServices);
