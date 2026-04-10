# creechr browser companion

a small chrome / edge extension that lets creechr (the desktop pet)
steal images and links out of webpages, then put them back. opt-in
per tab. requires the native messaging host to be installed (run
`install_extension_host.ps1` in the repo root before loading this).

## install (dev mode)

1. build creechr.exe and creechr-bridge.exe (see top-level readme)
2. run `..\install_extension_host.ps1` from a cmd / powershell window
   that's in this repo. this writes the host registration into HKCU
   for chrome and edge.
3. open `chrome://extensions` (or `edge://extensions`).
4. flip "developer mode" on.
5. click "load unpacked" and pick this `extension/` directory.
6. the extension's id will appear. copy it into the host json file
   if it's not already in the allowed_origins list. (yes, this is the
   stupidest part. yes, it's manifest v3's fault.)
7. open a tab, click the creechr icon, click "let him in".

## what creechr can steal

- `<img>` elements
- `<a href>` links with visible text or content
- `<button>` elements
- `<li>` list items

he removes them from the dom, walks them to a corner, drops them, then
walks back and puts them in the same parent at the same sibling
position. if he gets confused or you close the tab, the items are
just gone — sorry. (we don't try to restore across tab closures.)

## what creechr can NOT steal

- `<input>`, `<textarea>`, password fields. enforced in content.js.
- iframes. content scripts can't reach across origin boundaries
  for steals reliably and we don't try.
- anything in a tab you didn't opt in to.
