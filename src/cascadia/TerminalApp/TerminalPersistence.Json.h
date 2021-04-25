// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#pragma once

#include "TerminalPersistence.h"
#include "../TerminalSettingsModel/JsonUtils.h"

namespace Microsoft::Terminal::Settings::Model::JsonUtils
{
#define MTPM_XX_GET(NAME, TYPE) GetValueForKey(json, #NAME, val.NAME);
#define MTPM_XX_SET(NAME, TYPE) SetValueForKey(json, #NAME, val.NAME);
#define MTPM_XX(NAME, GENERATOR)                                               \
        template<>                                                             \
        class ConversionTrait<::Microsoft::Terminal::Persistence::Model::NAME> \
        {                                                                      \
            using Model = ::Microsoft::Terminal::Persistence::Model::NAME;     \
                                                                               \
        public:                                                                \
            Model FromJson(const Json::Value& json) const                      \
            {                                                                  \
                Model val;                                                     \
                GENERATOR(MTPM_XX_GET)                                         \
                return val;                                                    \
            }                                                                  \
                                                                               \
            bool CanConvert(const Json::Value&) const                          \
            {                                                                  \
                return true;                                                   \
            }                                                                  \
                                                                               \
            Json::Value ToJson(const Model& val) const                         \
            {                                                                  \
                using namespace ::Microsoft::Terminal::Persistence::Model;     \
                Json::Value json;                                              \
                GENERATOR(MTPM_XX_SET)                                         \
                return json;                                                   \
            }                                                                  \
                                                                               \
            std::string TypeDescription() const                                \
            {                                                                  \
                return #NAME;                                                  \
            }                                                                  \
        };
        MTPM_MODELS(MTPM_XX)
#undef MTPM_XX
#undef MTPM_XX_SET
#undef MTPM_XX_GET
}
