// Surround Sound Studio - which page owns the console's audio output.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "concepts/studio_audio.hpp"

namespace hui::concepts
{

namespace
{

struct Entry
{
    StopFn stop = nullptr;
    void *self = nullptr;
};

Entry g_pages[static_cast<int>(StudioPage::count)];

} // namespace

void studio_register(StudioPage page, StopFn stop, void *self)
{
    g_pages[static_cast<int>(page)] = {stop, self};
}

void studio_unregister(StudioPage page)
{
    g_pages[static_cast<int>(page)] = {};
}

void studio_enter(StudioPage page)
{
    for (int i = 0; i < static_cast<int>(StudioPage::count); ++i)
        if (i != static_cast<int>(page) && g_pages[i].stop != nullptr)
            g_pages[i].stop(g_pages[i].self);
}

} // namespace hui::concepts
