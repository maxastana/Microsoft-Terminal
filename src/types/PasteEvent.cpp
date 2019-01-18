/********************************************************
*                                                       *
*   Copyright (C) Microsoft. All rights reserved.       *
*                                                       *
********************************************************/

#include "precomp.h"
#include "inc/IInputEvent.hpp"

PasteEvent::~PasteEvent()
{
}

INPUT_RECORD PasteEvent::ToInputRecord() const noexcept
{
    INPUT_RECORD record{ 0 };
    // What to do?
    return record;
}

InputEventType PasteEvent::EventType() const noexcept
{
    return InputEventType::PasteEvent;
}

void PasteEvent::SetContent(const std::wstring_view content) noexcept
{
    _content = content;
}
