Ledger for macOS
================

Ledger, the accounting of Onyx -- the same program as on the Raspberry Pi (the same sources:
user/Apps/ledger, its toolkit, Writer for the printed documents), built for macOS on Apple silicon
(M1, M2, M3, M4...). The double-entry books of a Belgian company or self-employed person: sales and
purchases, bank and cash (CODA statements), quotes, orders and delivery notes, the VAT returns
(Intervat), the listings, SEPA payments, the reports. The manual: Help > Ledger Manual (also in
Ledger.app/Contents/Resources/sd/manuals/ledger: Ledger.pdf, Ledger.fr.pdf, Ledger.nl.pdf) --
everything in it holds on the Mac, but for the few lines below.

Needs: macOS 11 Big Sur or later (up to macOS 26 Tahoe), a Mac with Apple silicon.

Installing
  Drag Ledger.app to the Applications folder. It is not signed by an identified developer: the first
  time, macOS says it cannot check it -- open System Settings > Privacy & Security, scroll to
  "Ledger was blocked...", click Open Anyway, then Open. (In a Terminal, the same:
  xattr -dr com.apple.quarantine /Applications/Ledger.app.) A Ledger.app built on this Mac
  (pc/macOS/build.sh) opens at once.

Your files
  Documents/Onyx Ledger                       what the manual calls SD:/docs -- your books (.ledger),
                                              and what Ledger makes: Quotes, Orders, Invoices,
                                              Credit notes, Reports, VAT, Payments
  Library/Application Support/Onyx Ledger     the rest of Ledger's "SD card": its settings, the
                                              books opened last, the printing templates
                                              (apps/ledger.app/templates: copied there at the first
                                              start -- change them in Writer, Pages or Word)
  The demo company (File > Open: SD:/docs/demo-company.ledger) is in the application; changed and
  saved, it is written in Documents/Onyx Ledger.
  The file dialogs show three volumes: SD: (the above), HOME: (your home folder) and MAC: (the whole
  Mac: MAC:/Volumes/... for a USB key).

On the Mac
  - The window is a Mac window: resize it, full screen (Ctrl+Cmd+F); macOS remembers its place.
    The menus (File, Edit, Documents, Go, Tools) are in the menu bar.
  - The manual's Ctrl+key is Cmd+key: Cmd+N a new document, Cmd+S save, Cmd+F search, Cmd+O open,
    Cmd+C / Cmd+X / Cmd+V the clipboard (the Mac's). Cmd+Left / Right: the start / end of the line;
    Option is the manual's Alt (Option+Down opens a list). Accented letters are typed as usual.
  - A right click (or Ctrl+click, a two-finger click) is the right button.
  - Opening books: File > Open..., or drop a .ledger file on the window or on Ledger in the Dock,
    or double-click it in the Finder.
  - Printing (a quote, an invoice...): Writer -- inside Ledger.app -- makes the document from its
    template in Documents/Onyx Ledger/<Quotes, Invoices...> and shows it; to put it on paper or make
    a PDF, open that file in Pages, TextEdit or Word (File > Print; Save as PDF).
  - Reports exported "for the Spreadsheet" (.xlsx) open in Numbers or Excel; a folder Ledger shows
    opens in the Finder.

Built on a Mac from the Onyx repository: sh pc/macOS/build.sh (the Xcode command-line tools: see the
file's header). On Linux, sh pc/macOS/check.sh checks the port's file and program handling.
