---
author: Mike Griese @zadjii-msft
created on: 2021-03-03
last updated: 2021-03-03
issue id: #2634
---

# Boradcast Input

## Abstract

"Broadcast Input" is a feature present on other terminals which allows the user
to send the same input to multiple tabs or panes at the same time. This can make
it simpler for the user to run the same command in multiple directories or
servers at the same time.

With a viable prototype in [#9222], it's important that we have a well-defined
plan for how we want this feature to be exposed before merging that PR. This
spec is intended to be a lighter-than-usual spec to build consensus on the
design of how the actions should be expressed.

## Background

### Inspiration

This spec is heavily inspired by the [iTerm2 implementation]. @carlos-zamora did
a great job of breaking down how iTerm2 works in [this comment].

SecureCRT also implements a similar feature using a "chat window" that can send
the input in the chat window to all tabs. This seemed like a less ergonomic
solution, so it was not seriously considered.

Additionally, Terminator (on \*nix) allows for a similar feature through the use
of "groups". From [@zljubisic]:

> In Linux terminator you can define groups, and than put each pane in one of
> defined groups. Afterwards, you can choose broadcasting to all panes, only
> certain group or no broadcast at all.

This also seemed like a less powerful version of broadcast input than the
iterm2-like version, so it was also not further investigated.

### User Stories

iTerm2 supports the following actions:

* **Story A:** _Send input to current session only_: The default setting.
* **Story B:** _Broadcast to all panes in all tabs_: Anything you type on the
  keyboard goes to all sessions in this window.
* **Story C:** _Broadcast to all panes in current tab_: Anything you type on the
  keyboard goes to all sessions in this tab.
* **Story D:** _Toggle broadcast input to current session_: Toggles whether this
  session receives broadcasted keystrokes within this window.

### Future Considerations

This is supposed to be a quick & dirty spec, so I'm skipping this.

## Solution Design

### Proposal 1: iTerm2-like Modal Input Broadcast

iTerm2 implements broadcast input as a type of "modal" system. The user is in
one of the following modes:

* Broadcast to all panes in all tabs
* Broadcast to all panes in the current tab
* Broadcast to some set of panes within the current tab
* Don't broadcast input at all (the default behavior)

These modes are vaguely per-tab state. There's a global "broadcast to all tabs &
panes" property. Then, each tab also has a pair of values:
* Should input be sent to all panes in this tab?
* If not, which panes should input be sent to?

It's not possible to send input to one pane in tab A, then another pane in tab
B, without enabling the global "broadcast to everyone" mode.

This seems to break down into the following actions:

```json
{ "action": "toggleBroadcastInput", "scope": "global" },
{ "action": "toggleBroadcastInput", "scope": "tab" },
{ "action": "toggleBroadcastInput", "scope": "pane" },
{ "action": "toggleBroadcastInput", "scope": "none" },
```

Which would be accompanied by the following internal properties:
* A global (`TerminalPage`-level) property for `broadcastToAllPanesAndTabs`
* A per-tab property for `broadcastToAllPanes`
* A per-tab set of panes to broadcast to

* `"scope": "global"`: Toggle the global "broadcast to all tabs and panes"
  setting.
* `"scope": "tab"`: Toggle the tab's "broadcast to all panes in this tab"
  setting.
    - This does not modify the set of panes that the user is  broadcasting to in
      the tab, merely toggles the tab's setting. If the user has a set of panes
      they're broadcasting to in this tab, then toggles this setting on and off,
      we'll return to broadcasting to that set.
* `"scope": "pane"`: Add this pane to the set of panes being broadcasted to in
  this tab.
    - **TODO: FOR DISCUSSION**: Should this disable the tab's
      "broadcastToAllPanes" setting? Or should it leave that alone?
* `"scope": "none"`: Set the global setting to false, the tab's setting to
  false, and clear the set of panes being broadcasted to for this tab.

#### Pros
* This is exactly how iTerm2 does it, so there's prior art.
* If you're not globally broadcasting, then you're only ever broadcasting to
  some (sub)set of the panes in the current tab. So global broadcast mode is
  the only time a user would need to worry about input being to be sent to
  an inactive tab.
* You can have a set of panes to broadcast to in the first tab, then a
  _separate_ set to broadcast to in a second tab. Broadcasting in one tab
  does not affect the other.

#### Cons
* I frankly think the `tab`/`pane` interaction can be a little weird.
* You can't broadcast to a subset of panes in inactive tabs, in addition to
  the active tab. All panes you want to broadcast to must be in the active
  tab.
* Does creating a new split in a pane that's being broadcast to add that pane to
  the broadcast set?

#### What would this mean for PR #9222?

The prototype PR [#9222] basically just implemented `{ "action":
"toggleBroadcastInput", "scope": "tab" }`. We could make `tab` the default
`scope` if no other one is specified, and then the PR would need basically no
modifications. Future PRs could add args to the `toggleBroadcastInput` action,
without breaking users who bind a key to that action now.

### Proposal 2: Broadcast Set

This was the design I had originally came up with before investigating iTerm2
much closer. This design involves a "broadcast set" of panes. All the panes in
the broadcast set would also get the `KeySent` and `CharSent` events, in
addition to the active pane. (The active pane may be a part of the broadcast
set). If a pane is read-only in the broadcast set, then it won't handle those
broadcasted events (obviously).

As far as actions, we're looking at something like:

* **A** Only send input to the active pane
  * Remove all the panes from the broadcast set
* **B** send input to all panes in all tabs
  * If all the panes are in the broadcast set, remove them all. Otherwise, add
    all panes in all tabs to the broadcast set.
* **C** send input to all panes in the current tab
  * If all the panes in the current tab are in the broadcast set, remove them
    from the broadcast set. Otherwise, add all the panes from this tab to the
    broadcast set.
* **D** toggle sending input to the current pane
  * If this pane is in the broadcast set, remove it. Otherwise add it.
This seems to break down into the following actions:

```json
{ "action": "toggleBroadcastInput", "scope": "none" },
{ "action": "toggleBroadcastInput", "scope": "global" },
{ "action": "toggleBroadcastInput", "scope": "tab" },
{ "action": "toggleBroadcastInput", "scope": "pane" },
```

Which would be accompanied by the following internal properties:
* A global (`TerminalPage`-level)  set of panes to broadcast to.

#### Pros:
* Mentally, you're either adding panes to the set of panes to broadcast to, or
  removing them.
* You can broadcast to panes in multiple tabs, without broadcasting to _all_
  panes in all tabs.

#### Cons:
* You can't have a set of panes to broadcast to in the one tab, and a different
  set in another tab.
* is _slightly_ different from iTerm2.
* Does creating a new split in a pane that's being broadcast to add that pane to
  the broadcast set?

#### What would this mean for PR #9222?

Similar to Proposal 1, we'd use `tab` as the default value for `scope`. In the
future, when we add support for the other scopes, we'd change how the
broadcasting works, to use a set of panes to broadcast to, instead of just the
tab-level property.

### Proposal 3: It's iTerm2, but slightly different

While typing this up, I thought maybe it might make more sense if we took the
iTerm2 version, and changed it slightly:

* `"scope": "tab"`: If all the panes are in the broadcast set for this tab, then
  remove them all. Otherwise, add all the panes in this tab to this tab's
  broadcast set.
* `"scope": "pane"`: If this pane is in the broadcast set for a tab, then remove
  it. Otherwise, add it.

With this, we get rid of the tab-level setting for "broadcast to all the panes
in this tab", and rely only on the broadcast set for that tab.

#### Pros:
* All the pros from proposal A
* Does away with the seemingly weird toggling between "all the panes in a tab"
  and "some of the panes in a tab" that's possible with proposal A

#### Cons:
* You can't broadcast to a subset of panes in inactive tabs, in addition to
  the active tab. All panes you want to broadcast to must be in the active
  tab.
* is _slightly_ different from iTerm2. Just _slightly_.
* Does creating a new split in a pane that's being broadcast to add that pane to
  the broadcast set?

#### What would this mean for PR #9222?

Same as with proposal A, we wouldn't change anything in the current PR. A future
PR that would add the other scope's to that action would need to change how the
broadcasting within a tab works, to use a set of panes to broadcast to, instead
of just the tab-level property.

## Conclusion

I'm proposing these settings for broader discussion. I'm not really sure which I
like most at this point. I'm maybe leaning towards 1 or 3? That might be just
because of the prior art though. Might be worthwhile to investigate if there are
bug reports on iTerm2 or open feature requests related to this functionality.

**TODO**: Make a decision.

_**Fortunately**_: All these proposals actually use the same set of actions. So
it doesn't _really_ matter which we pick right now. We can unblock [#9222] as
the implementation of the `"tab"` scope, and address other scopes in the future.
We should still decide long-term which of these we'd like, but the actions seem
universal.

## UI/UX Design

This is supposed to be a quick & dirty spec, so I'm LARGELY skipping this.

As far as indicators go, we'll throw something like:

![NetworkTower Segoe UI Icon](broadcast-segoe-icon.png)

in the tab when a pane is being broadcasted to. If all tabs are being
broadcasted to, then they'll all have that icon.

It probably makes the most sense to have pane titlebars (#4998) also display
that icon.

In the original PR, it was suggested to use some variant of the [accent color]
to on the borders of panes that are currently recieving broadcasted input. This
would be a decent visual indicator that they're _not_ the active pane, but they
are going to recieve input. Something a bit like:

![A sample of using the border to indicate the broadcasted-to panes](broadcast-input-borders.gif)

## Potential Issues

<table>

<tr>
<td><strong>Compatibility</strong></td>
<td>

[comment]: # Will the proposed change break existing code/behaviors? If so, how, and is the breaking change "worth it"?

</td>
</tr>
</table>

[comment]: # If there are any other potential issues, make sure to include them here.


## Resources

[comment]: # Be sure to add links to references, resources, footnotes, etc.


### Footnotes

<a name="footnote-1"><a>[1]:


[#2634]: https://github.com/microsoft/terminal/issues/2634
[#4998]: https://github.com/microsoft/terminal/issues/4998
[#9222]: https://github.com/microsoft/terminal/pull/9222
[this comment]: https://github.com/microsoft/terminal/issues/2634#issuecomment-789116413
[iTerm2 implementation]: https://iterm2.com/documentation-one-page.html#documentation-menu-items.html
[@zljubisic]: https://github.com/microsoft/terminal/pull/9222#issuecomment-789143189
[accent color]: https://docs.microsoft.com/en-us/windows/uwp/design/style/color#accent-color-palette
