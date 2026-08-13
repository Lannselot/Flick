// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

class PlatformServices;

std::unique_ptr<PlatformServices> createMacPlatformServices();
