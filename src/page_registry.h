// src/page_registry.h
// 栖屏 DeskNest - lightweight page registry
//
// 当前 MVP 只把竖屏页面组接入运行路径；横屏页面保留给后期
// boot-time landscape display orientation adapter。

#ifndef DESKNEST_PAGE_REGISTRY_H
#define DESKNEST_PAGE_REGISTRY_H

#include "config.h"

#include <stdint.h>

namespace desknest {

enum PageGroup : uint8_t {
    PAGE_GROUP_PORTRAIT = 0,
    PAGE_GROUP_LANDSCAPE,  // reserved
    PAGE_GROUP_SPECIAL,
};

struct PageDef {
    UIPage id;
    PageGroup group;
    const char* title;
    bool showInPageDots;
};

static const PageDef DN_PAGE_REGISTRY[] = {
    { PAGE_PORTRAIT_OVERVIEW,    PAGE_GROUP_PORTRAIT,  "DeskNest",    true  },
    { PAGE_PORTRAIT_AI_USAGE,    PAGE_GROUP_PORTRAIT,  "AI Usage",    true  },
    { PAGE_PORTRAIT_ENVIRONMENT, PAGE_GROUP_PORTRAIT,  "Environment", true  },
    { PAGE_PORTRAIT_SETTINGS,    PAGE_GROUP_PORTRAIT,  "Settings",    true  },

    // Reserved for future boot-time landscape initialization mode.
    { PAGE_LANDSCAPE_OVERVIEW,   PAGE_GROUP_LANDSCAPE, "DeskNest",    true  },
    { PAGE_LANDSCAPE_FOCUS,      PAGE_GROUP_LANDSCAPE, "Focus",       true  },
    { PAGE_LANDSCAPE_CUSTOM,     PAGE_GROUP_LANDSCAPE, "Custom",      true  },

    { PAGE_SLEEP_FACE_DOWN,      PAGE_GROUP_SPECIAL,   "Roost",       false },
    { PAGE_CONFIG_PORTAL,        PAGE_GROUP_SPECIAL,   "WiFi Setup",  false },
};

static const uint8_t DN_PAGE_REGISTRY_COUNT =
    (uint8_t)(sizeof(DN_PAGE_REGISTRY) / sizeof(DN_PAGE_REGISTRY[0]));

inline const PageDef* dn_find_page_def(UIPage page) {
    for (uint8_t i = 0; i < DN_PAGE_REGISTRY_COUNT; ++i) {
        if (DN_PAGE_REGISTRY[i].id == page) return &DN_PAGE_REGISTRY[i];
    }
    return nullptr;
}

inline const char* dn_page_title_from_registry(UIPage page) {
    const PageDef* def = dn_find_page_def(page);
    return def ? def->title : "";
}

inline uint8_t dn_page_group_count(PageGroup group) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < DN_PAGE_REGISTRY_COUNT; ++i) {
        if (DN_PAGE_REGISTRY[i].group == group) ++count;
    }
    return count;
}

inline UIPage dn_first_page_in_group(PageGroup group) {
    for (uint8_t i = 0; i < DN_PAGE_REGISTRY_COUNT; ++i) {
        if (DN_PAGE_REGISTRY[i].group == group) return DN_PAGE_REGISTRY[i].id;
    }
    return PAGE_PORTRAIT_OVERVIEW;
}

inline int8_t dn_index_in_group(PageGroup group, UIPage page) {
    int8_t index = 0;
    for (uint8_t i = 0; i < DN_PAGE_REGISTRY_COUNT; ++i) {
        if (DN_PAGE_REGISTRY[i].group != group) continue;
        if (DN_PAGE_REGISTRY[i].id == page) return index;
        ++index;
    }
    return -1;
}

inline UIPage dn_page_at_group_index(PageGroup group, uint8_t index) {
    uint8_t seen = 0;
    for (uint8_t i = 0; i < DN_PAGE_REGISTRY_COUNT; ++i) {
        if (DN_PAGE_REGISTRY[i].group != group) continue;
        if (seen == index) return DN_PAGE_REGISTRY[i].id;
        ++seen;
    }
    return dn_first_page_in_group(group);
}

inline UIPage dn_next_page_in_group(PageGroup group, UIPage current) {
    const uint8_t count = dn_page_group_count(group);
    if (count == 0) return PAGE_PORTRAIT_OVERVIEW;
    const int8_t currentIndex = dn_index_in_group(group, current);
    if (currentIndex < 0) return dn_first_page_in_group(group);
    return dn_page_at_group_index(group, (uint8_t)((currentIndex + 1) % count));
}

inline UIPage dn_prev_page_in_group(PageGroup group, UIPage current) {
    const uint8_t count = dn_page_group_count(group);
    if (count == 0) return PAGE_PORTRAIT_OVERVIEW;
    const int8_t currentIndex = dn_index_in_group(group, current);
    if (currentIndex < 0) return dn_first_page_in_group(group);
    const uint8_t prevIndex = (currentIndex == 0)
        ? (uint8_t)(count - 1)
        : (uint8_t)(currentIndex - 1);
    return dn_page_at_group_index(group, prevIndex);
}

} // namespace desknest

#endif // DESKNEST_PAGE_REGISTRY_H

