// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include <string>
#include <vector>
#include "json.h"

// M(icrosoft) T(erminal) P(ersistence) M(odel)
#define MTPM_ROOT_FIELDS(XX) \
    XX(tabs, std::vector<Tab>)

#define MTPM_TAB_FIELDS(XX) \
    XX(profile, std::wstring)

#define MTPM_MODELS(XX) \
    XX(Root, MTPM_ROOT_FIELDS) \
    XX(Tab, MTPM_TAB_FIELDS)

namespace Microsoft::Terminal::Persistence
{
    namespace Model
    {
#define MTPM_XX(NAME, GENERATOR) struct NAME;
        MTPM_MODELS(MTPM_XX)
#undef MTPM_XX

#define MTPM_XX_SUB(NAME, TYPE) TYPE NAME;
#define MTPM_XX(NAME, GENERATOR)   \
        struct NAME {              \
            GENERATOR(MTPM_XX_SUB) \
        };
        MTPM_MODELS(MTPM_XX)
#undef MTPM_XX
#undef MTPM_XX_SUB
    }
}
