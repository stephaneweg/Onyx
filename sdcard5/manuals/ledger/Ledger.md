# Ledger — User Manual

*Double-entry accounting for Belgian companies and the self-employed, on Onyx*

Edition of September 2026, for Ledger as shipped with Onyx. The pictures show the demo company that
comes with Ledger, *Atelier Lumen SRL*, a small design studio in Brussels.

## Contents

- [1. Introduction](#1-introduction)
- [2. Getting started](#2-getting-started)
- [3. The window](#3-the-window)
- [4. Setting up your books](#4-setting-up-your-books)
- [5. Customers and suppliers](#5-customers-and-suppliers)
- [6. Sales](#6-sales)
- [7. Purchases](#7-purchases)
- [8. Paying your suppliers (SEPA)](#8-paying-your-suppliers-sepa)
- [9. Bank and cash](#9-bank-and-cash)
- [10. Miscellaneous operations](#10-miscellaneous-operations)
- [11. Quotes, orders and delivery notes](#11-quotes-orders-and-delivery-notes)
- [12. Printing documents from templates](#12-printing-documents-from-templates)
- [13. Reports](#13-reports)
- [14. VAT](#14-vat)
- [15. Closing the fiscal year](#15-closing-the-fiscal-year)
- [16. Files and backups](#16-files-and-backups)
- [17. Questions and answers](#17-questions-and-answers)
- [18. Glossary](#18-glossary)
- [Appendix A. The VAT codes](#appendix-a-the-vat-codes)
- [Appendix B. The merge fields](#appendix-b-the-merge-fields)
- [Appendix C. Keyboard reference](#appendix-c-keyboard-reference)

## 1. Introduction

### 1.1 What Ledger does

Ledger keeps the complete double-entry books of a Belgian company — an SRL, an SA, an SC, an
association — or of a self-employed person. It follows the Belgian chart of accounts, the **PCMN**
(*Plan comptable minimum normalisé*; in Dutch the *MAR*, *Minimum Algemeen Rekeningenstelsel*), and the
Belgian VAT rules, and it produces the files the Belgian administration and banks work with:

- sales and purchase **invoices** and **credit notes**, with every Belgian VAT case: the 6, 12 and 21 %
  rates, intra-Community supplies and services, exports, the co-contractor's reverse charge,
  non-deductible VAT, the company car's 50 %;
- **quotes**, **orders**, **delivery notes** and **purchase orders**, each one turned into the next and
  finally into the invoice;
- documents **printed from templates** by Letters, in French, Dutch or English, with your letterhead;
- **bank and cash statements**, typed or **imported from your bank's CODA files**, the invoices they
  pay found and matched for you;
- **payments to your suppliers** as a **SEPA** credit transfer file for your bank;
- the periodic **VAT return** as an **Intervat** XML file, the **annual customer listing** and the
  **intra-Community listing**;
- **reports**: journals, general ledger, trial balance, balance sheet, income statement, customers'
  and suppliers' balances, receivables and payables by age — on screen, as Letters documents or as
  spreadsheets;
- the **year-end closing**, the year's result carried forward.

You type **documents** — an invoice, a bank statement — and never raw entries: Ledger builds the
accounting entry, shows it while you type and posts it when you save. Everything is written to the
company's file at once: there is no separate "save" step for the books.

### 1.2 Who it is for

Ledger is made for the people who keep their own books or those of a small business: a freelance
designer, a consultant, a small shop, a small company with a few hundred invoices a year. Its features
follow those of **BOB 50**, the Belgian accounting package, with the simplicity of **GnuCash**. You do
not need to be an accountant to use it, but you should know the basics of double-entry bookkeeping:
section 1.5 recalls them.

> **Important.** Ledger helps you keep correct books and prepare your VAT files. It does not replace
> the advice of your accountant (*expert-comptable*, *accountant-boekhouder*): have your year-end
> figures checked by a professional before you file your annual accounts.

### 1.3 What you need

- Onyx, on a Raspberry Pi 4 (or the Onyx desktop simulator on a PC).
- **Letters**, installed with Onyx: it prints your quotes, invoices and reports.
- The **Spreadsheet** (optional): it opens reports as workbooks.
- To import bank statements: the **CODA** files your bank provides (from your online banking, usually
  `.cod` or `.txt` files), copied to the SD card.
- To file VAT returns: an access to **Intervat**, the SPF Finances' site, where you upload the XML files
  Ledger writes.

### 1.4 About this manual

- Buttons, menus and fields are written in **bold**, as on the screen: **Save**, **New invoice**.
- A menu command is written **File ▸ Open...**: the **File** menu, then its **Open...** item.
- Keys are written `Ctrl+N`, `Tab`, `Enter`, `Esc`, `F4`.
- Amounts are written the Belgian way: `1.234,56` — a dot groups the thousands, a comma separates the
  cents. Dates are written `28/09/2026`.
- The examples use the **demo company**, *Atelier Lumen SRL* (section 2.2). You can try everything on
  it: it is an ordinary file of its own, separate from your books.

### 1.5 Bookkeeping in a nutshell

- **Accounts.** Every amount is recorded on the accounts of the chart. The PCMN numbers them by class:
  **1** equity and long-term debts, **2** fixed assets, **3** stocks, **4** receivables and payables
  (400000 customers, 440000 suppliers, 451000 VAT payable, 411000 VAT recoverable...), **5** bank and
  cash (550000, 570000), **6** charges, **7** income. Classes 1 to 5 make the **balance sheet**,
  classes 6 and 7 the **income statement**.
- **Double entry.** Every operation is an **entry** whose **debits** equal its **credits**. A sales
  invoice of 1.000,00 plus 210,00 VAT debits the customer 1.210,00 (he owes it to you) and credits the
  sales 1.000,00 (an income) and the VAT payable 210,00 (you owe it to the State). Ledger writes these
  lines for you.
- **Journals.** Entries are kept in **journals** by their kind — sales, purchases, bank, cash,
  miscellaneous operations —, each numbering its documents from 1 every fiscal year.
- **Debit and credit balances.** An account shows a **debit** balance (D) when its debits are more
  than its credits — an asset, a charge, a customer who owes you — and a **credit** balance (C)
  otherwise — equity, a debt, an income.
- **Matching** (*lettrage*, *afpunten*). An invoice and the payment that settles it are **matched**:
  together they come to zero on the party's account, and the invoice shows **Paid**. Ledger matches
  them when you record a payment against its invoice.
- **Fiscal year.** The period the accounts are closed for, usually the calendar year. At its end, its
  **result** — the income less the charges — is carried forward to the equity.

## 2. Getting started

### 2.1 Starting Ledger

Start **Ledger** from the Applications list (group **Productivity**) or the dock. Ledger also opens when
you double-click a `.ledger` file in the File Viewer, or drop one on its window.

When it starts, Ledger opens the books it had open last time. The very first time, it shows its
welcome page.

![The welcome page](images/welcome.png)
*The welcome page: create your company, open existing books, or try the demo company.*

### 2.2 Trying the demo company

Click **Try the demo company** to open `SD:/docs/demo-company.ledger`: *Atelier Lumen SRL*, a design
studio with 23 customers and suppliers and about 330 documents from January 2025 to September 2026 —
invoices, credit notes, bank statements, VAT returns filed, quotes and orders. Fiscal year 2025 is
closed; 2026 is running. The demo is the best way to learn Ledger: every picture in this manual comes
from it.

The demo also comes with a bank file, `SD:/docs/demo-bank-statement.cod`, to try the CODA import
(section 9.3).

### 2.3 Creating your company

Click **Create a company...** on the welcome page, or choose **File ▸ New Company...**.

![The New company dialog](images/new-company.png)
*A new company: its name, VAT number, address, bank account, chart, VAT situation and first fiscal year.*

1. Type the company's **name** with its legal form (*Studio Nova SRL*), its **VAT number**
   (`BE 0456.789.034`: Ledger checks it), its **street**, **postcode** and **city**, **e-mail**,
   **phone** and **bank account** (IBAN).
2. Choose the **chart of accounts**: **Français (PCMN)** or **Nederlands (MAR)**. The chart's language
   is also the language of your documents, unless a customer's card says otherwise.
3. Choose the **VAT** situation:
   - **Files VAT returns**, **Quarterly** or **Monthly**: the usual case;
   - **Small business franchise** (*régime de la franchise*, *vrijstellingsregeling*): you charge no
     VAT and file no periodic return;
   - **Not subject to VAT**: for an activity exempted by article 44 of the VAT code (a doctor, a
     training centre...).
4. Type the **first fiscal year**, its first and last day (`01/01/2026` to `31/12/2026`). A fiscal year
   may last up to 24 months (a company's first one often does).
5. Click **Create** and choose where to save the file: `SD:/docs/Studio Nova SRL.ledger` is proposed.

Ledger makes the books: the whole PCMN in the chosen language (almost 500 accounts), the journals —
**VEN** sales, **ACH** purchases, **BNK** bank, **CAI** cash, **OD** miscellaneous operations (in
Dutch: **VKP**, **AKP**, **BNK**, **KAS**, **DIV**) — and the first fiscal year. Everything can be
changed afterwards in **Settings** (section 4).

> **Tip.** Then go to **Settings ▸ Company** to add your legal form, register (*RPM Bruxelles*), BIC and
> web site: they appear on your printed documents. And to **Settings ▸ Journals** to type your bank
> account's IBAN in the **BNK** journal: the CODA import and the supplier payments need it.

### 2.4 Opening books, several companies

Each company has its own file. **File ▸ Open...** (`Ctrl+O`) opens another company's books; the one
open is closed (it needs no saving). Keep as many companies as you like, one file each.

**File ▸ Save a Copy As...** writes a copy of the books under another name — before an experiment, or
to give your accountant a copy.

The file's name is shown at the foot of the side bar, with a green dot. The dot turns red if the file
could not be written (the card full or write-protected): the books are then kept in memory, and
Ledger tells you so — save a copy elsewhere at once.

## 3. The window

### 3.1 Its parts

![The parts of Ledger's window](images/window-parts.png)
*The Overview page of the demo company, and the parts of the window.*

1. **The company** open: its name and VAT number.
2. **The fiscal year shown.** The lists, the reports and the VAT page show this year; choose another
   one here. New documents are dated in it.
3. **The pages**, grouped: the **Overview**; the journals — **Sales**, **Purchases**, **Bank and cash**,
   **Misc. operations**; **Quotes and orders**; the parties — **Customers**, **Suppliers**; the
   accounting — **Chart of accounts**, **Reports**, **VAT**; and **Settings**. A red **badge** counts
   what needs you: invoices overdue on **Sales** (orange on **Purchases**: your own late payments), a
   VAT return late on **VAT**.
4. **The file** of the books, and its state (green: written).
5. **The page's title**, and a line about it (here the fiscal year).
6. **The page's buttons.** The one in colour is its main action (here: a new sales invoice).
7. **The Overview's tiles**: what your customers owe you (and how much of it is overdue), what you owe
   your suppliers, the bank and cash, the year's result and turnover.
8. **The sales and purchases** of the year, month by month (excluding VAT).
9. **The next VAT return** (its period, due date and amount so far) and **the invoices most overdue**
   — click one to open it.

### 3.2 Lists

Most pages are lists: the sales, the statements, the customers...

- **Filters** at the top left: **All**, **Open**, **Overdue**, **Paid** for invoices.
- The **Search** box at the top right finds words anywhere in a row: a name, a description, a number,
  an amount (`1.657,70` or `1657.70`). Several words must all be found. `Ctrl+F` goes to it.
- Click a **column's title** to sort by it; click again to reverse the order.
- **Double-click** a row, or press `Enter`, to open it. `Delete` deletes the chosen row (when it may be
  deleted, and after asking).
- **Right-click** a row for its other commands.
- The **foot** of the list adds up the rows shown.

### 3.3 Documents

A document — an invoice, a statement, an operation, a quote — opens **in place of its list**. Its
fields are at the top, its **lines** in a grid below, its totals at the bottom.

- **Save** (`Ctrl+S`) posts the document and goes back to the list; **Save & New** posts it and starts
  the next one; **Cancel** (`Esc`) leaves it — Ledger asks first when something was typed; the red
  **bin** deletes it.
- A field for a **party**, an **account** or a **VAT code** finds as you type: a name, a code, a VAT
  number, an account's number or words of its name. The list of what matches drops below; `Enter` or
  `Tab` takes the one lit, `F4` or `Alt+↓` shows them all.
- **Dates** can be typed `28/09/2026`, `28.9.26`, `28/9` (this year) or `280926`; the button at the
  right of a date field opens a calendar.
- **Amounts** can be typed `1850`, `1850,00`, `1.850,00` or `1850.00`.

![Finding a customer as you type](images/party-picker.png)
*A party found as you type: "Brou" finds Brouwerij De Klok NV (its code, its city).*

In the **grid of lines**, you type as in a spreadsheet:

| Key | What it does |
|---|---|
| `Tab`, `Enter` | Take what was typed, go to the next cell; after the last one, a new line |
| `Shift+Tab` | The cell before |
| `↑`, `↓` | The line above, below |
| `Esc` | Give back what the cell held (again: leave the grid) |
| `F4`, `Alt+↓` | The list of what can be typed in the cell |
| `Ctrl+Delete` | Remove the line (so does the cross at its end) |

A cell that cannot take what you typed is outlined in red, and the reason is shown at the foot of the
window.

### 3.4 Locked documents

A document cannot be changed or deleted once:

- its **fiscal year is closed** (section 15), or
- it holds VAT and the **VAT return of its period is marked filed** (section 14.3).

Its page then says **Locked** and why. To correct it, reopen the period or the year — or, better, post
a correcting document (a credit note, a miscellaneous operation) in the current period.

Sales invoices are numbered without gaps, as the law requires: only the **last** sales document of a
journal can be deleted. To cancel another one, make a **credit note** for it (section 6.5).

## 4. Setting up your books

### 4.1 The company

**Settings ▸ Company** holds what your documents print as their letterhead and what your VAT files
declare.

![Settings, the company](images/settings-company.png)
*The company's details: name, legal form, address, VAT number, contact, bank, register, web site, VAT situation.*

- **Name** and **Legal form** (*SRL*, *SA*, *SC*, *ASBL*...).
- **Street**, **City** (postcode and city), **Country** (`BE`).
- **VAT number**: checked as you type (**Valid**, **Wrong check digits**, **Not a VAT number**).
- **E-mail**, **Phone**, **Web site**.
- **IBAN** and **BIC**: printed on your invoices.
- **Register**: the register of legal entities and its court, *RPM Bruxelles*, *RPR Gent*.
- **VAT situation** and **Returns**: as at the creation (section 2.3); change them if your situation
  changes.

Click **Save the changes**.

### 4.2 Fiscal years

![Settings, the fiscal years](images/settings-years.png)
*The fiscal years: their dates, their state and their result.*

The list shows each fiscal year, its dates, its state (**Open** or **Closed**) and its **result**
(profit or loss so far).

- **Add the next year** adds a year after the last one, as long as it.
- **Close the year...** and **Reopen the year**: see section 15.

The balance sheet's accounts carry on from year to year by themselves: you never type an opening entry
between two years of the same books.

### 4.3 Journals

![Settings, the journals](images/settings-journals.png)
*The journals: their code, name, kind, account and IBAN.*

A company needs one journal of each kind; add others when you want to keep documents apart — a second
bank account, a second sales series (**VEN2** for a shop).

![A journal](images/journal-dialog.png)
*A bank journal: its code, name, kind, account (55xxxx) and IBAN.*

Double-click a journal (or choose it and click **Edit...**), or click **New journal...**:

- **Code**: 1 to 5 letters or digits, printed in the document numbers (*VEN 2026/0054*).
- **Name** and **Kind**: sales, purchases, bank, cash or miscellaneous (a journal's kind cannot be
  changed once it has documents).
- **Account**: a bank journal's account (550000, 550100 for a second bank...), a cash journal's
  (570000).
- **IBAN**: the bank account's number. **The CODA import finds a statement's journal by it**, and the
  supplier payments are made from it.
- **Hidden**: a journal no longer offered for new documents (its documents stay).

### 4.4 The accounts by role

![Settings, the accounts by role](images/settings-accounts.png)
*The accounts Ledger uses for its own entries.*

Ledger posts some lines by itself — the customer's and supplier's side of an invoice, the VAT, the
year's result. **Settings ▸ Accounts** says on which accounts: **Customers** (400000), **Suppliers**
(440000), **VAT due** (451000), **VAT deductible** (411000), **Profit carried forward** (140000), **Loss
carried forward** (141000) and the **Suspense account** (499000). Keep the PCMN's accounts unless your
accountant asks otherwise.

### 4.5 The chart of accounts

**Chart of accounts** shows the PCMN as a tree — classes, groups, accounts — with each account's
balance at the end of the fiscal year shown. Below, the chosen account's **register**: its lines of the
year, the balance brought forward and the running balance.

![The chart of accounts](images/chart.png)
*The chart: the accounts matching "7001"; the register of account 700100, the year's sales of services.*

- **With movements** shows only the accounts used this year; **All** the whole chart.
- The search box finds an account by the start of its number (`7001`) or words of its name
  (`leasing`).
- Double-click a class or a group to fold or unfold it.
- Double-click a line of the register to open its document.

**New account** adds an account: its **number** (usually 6 digits, classes 0 to 7, not already in the
chart) and its **name**. **Edit** changes the chosen account:

![An account](images/account-dialog.png)
*An account's card: its name, the nature of its purchases, hidden or not.*

- **Name**.
- **Purchases**: the nature of what is bought on this account, which decides the VAT return's grid of
  its purchases — **Goods** (grid 81), **Services** (grid 82), **Investments** (grid 83), or **By its
  number** (60 goods, 61 services, 2x investments...).
- **Hidden (no longer offered)**: the account stays in the chart and the reports but is no longer
  proposed as you type.

### 4.6 Starting with existing books

When you move to Ledger during a company's life, bring the balances over with one **miscellaneous
operation** (section 10) dated the **first day** of your first fiscal year in Ledger, and tick
**Opening balances**:

- one line per balance sheet account with its balance — the capital (100000) and the reserves on the
  credit side, the equipment (2x) and the bank (550000) on the debit side...;
- one line **per open invoice** on 400000 with its customer (debit) or on 440000 with its supplier
  (credit), so that each can be matched with its payment later;
- the difference, if any, on 499000 (the suspense account), to be cleared with your accountant.

Take the figures from the previous accounts' closing balance sheet and open items.

## 5. Customers and suppliers

### 5.1 The lists

**Customers** and **Suppliers** list the parties with their **code**, **name**, **VAT number**, **city**,
**balance** and what is **overdue**. The filters show **All**, those **With a balance** or those
**Overdue**; the search box finds a code, a name, a city, a VAT number or an e-mail.

![The customers](images/customers.png)
*The customers; below, the chosen one's account: its invoices and payments, matched (the link) or open.*

Below the list, the chosen party's **account** for the fiscal year shown: the balance brought forward,
each invoice, credit note and payment, their **due** date (in red when past) and the **running
balance** (D: the party owes you, C: you owe the party). A link icon marks the lines already
**matched**. Double-click a line to open its document.

### 5.2 A party's card

Click **New customer** (or **New supplier**), or double-click a party, or choose it and click **Card**.
A new party can also be made while typing a document: the **+** button at the right of its party
field.

![A customer's card](images/customer-card.png)
*A customer's card: its code, payment terms, VAT number and situation, language, address, bank, usual account.*

- **Name**, and a **Code** — made from the name when left empty (*BROUWERI*); you may type your own.
- **Payment terms**: the days until an invoice is due (30 by default). An invoice's due date follows.
- **VAT number**: checked as you type — **Valid Belgian number**, **EU number (its form)**, **Its check
  digits are wrong**. Typing it also fills the **VAT situation** and the **Country**.
- **VAT situation**: it decides the VAT codes proposed on the party's documents (section 6.3):
  - **Belgian, subject to VAT**: the normal rates (V21, A21...);
  - **Private person (no VAT number)**: the normal rates; the customer is left out of the customer
    listing;
  - **Other EU country, VAT number**: intra-Community (VEUS services, VEUG goods for a customer;
    AEUS21, AEU21 for a supplier);
  - **Outside the EU**: exports (VEX), or services from abroad (AWS21);
  - **Belgian co-contractor (reverse charge)**: building works, the customer pays the VAT (VCC,
    ACC21).
- **Language**: **French**, **Dutch** or **English** — the language of the party's printed quotes,
  orders and invoices.
- **Street**, **Postcode**, **City**, **Country** (its code: `BE`, `FR`, `NL`...).
- **E-mail**, **Phone**.
- **IBAN** and **BIC**: a supplier's account is needed to pay it by SEPA (section 8); a customer's
  lets the CODA import recognise its payments.
- **Account**: the account proposed on its documents' lines (a customer: 700100 *services in
  Belgium*; a supplier: 612000 *rent*...).
- **VAT code**: a code of its own, else **By its situation**.
- **Notes**: anything to remember.

To delete a party, choose it and press `Delete`. A party that has documents, quotes or orders cannot
be deleted: its card stays with them in the books.

### 5.3 Matching

Ledger matches an invoice with its payment when you record the payment (section 9.2): nothing to do
most of the time. To match lines yourself — an invoice paid in two parts, a credit note that offsets
an invoice:

1. In the party's account, tick the lines (click their box, or `Space`). The **Match** button shows the
   sum of the lines ticked.
2. When they come to **zero**, click **Match**: they are marked with a link and the invoice shows
   **Paid**.

**Unmatch** undoes the matching of the chosen line. **Open items only** shows only the lines not yet
matched, of every year.

## 6. Sales

### 6.1 The sales

**Sales** lists the sales invoices and credit notes of the fiscal year: their **number**, **date**,
**customer**, **description**, **total** and **status**.

![The sales](images/sales.png)
*The sales of 2026: paid, due, late, a credit note settled.*

| Status | Meaning |
|---|---|
| **Paid** | The invoice is matched with its payment |
| **Due 26/10** | Open, due on that day |
| **4 days late** | Open and past its due date |
| **Settled** | A credit note matched (with its invoice or a refund) |
| **Credit open** | A credit note not yet used |

A credit note is marked **CN** after its number. **All**, **Open**, **Overdue** and **Paid** filter the
list; when a kind of document has several journals, a list chooses one of them or **All the
journals**.

### 6.2 A sales invoice, step by step

Click **New invoice** (`Ctrl+N`), or **Invoice** on the Overview, or **Documents ▸ Sales Invoice**.

![A new sales invoice, typed](images/invoice-parts.png)
*A sales invoice being typed: two lines at 21 %; below, the entry it will post and its totals.*

1. **Customer**: type a few letters of its name, its code or its VAT number and take it from the list
   (or click **+** to make a new card). Its address and VAT number show below.
2. **Date** and **Due date**: today (or the fiscal year's nearest day), and the date plus the customer's
   payment terms; the number of days is shown.
3. **Description**: what the invoice is for (*Beer labels: design and proofs*); it shows in the lists
   and on the printed invoice.
4. **Kind**: **Invoice** or **Credit note**.
5. **Communication**: the Belgian **structured communication** (+++123/4567/89012+++) the customer
   will put on the payment. Leave it empty: Ledger makes it from the invoice's number when it saves
   it, and prints it on the invoice. Your bank's CODA files then give it back with the payment, which
   finds its invoice.
6. **Reference**: the customer's order number or reference, printed on the invoice.
7. **The lines**: for each one, the **account** (the customer's usual one is proposed: 700100...), a
   **description**, the amount **excluding VAT** and the **VAT code** (proposed from the customer's
   situation, section 6.3). The **VAT** is computed — type another amount when the invoice says
   otherwise (it is then shown in blue) — and so is the line's **total**.
8. **The entry it makes**: the customer debited, the sales and the VAT credited — as it will be
   posted.
9. **The totals**: excluding VAT, the VAT of each code, the **total to pay**.
10. **Save** posts the invoice and goes back to the list; **Save & New** posts it and starts the next
    one.

The invoice gets the journal's next number (*VEN 2026/0062*). It now shows in the customer's account,
in the VAT return of its period and in every report.

![A saved invoice, overdue](images/invoice.png)
*A saved invoice: "Open, overdue since 24/09/2026", its structured communication; Print makes it in Letters.*

### 6.3 The VAT codes of sales

| Code | Use it for | VAT return |
|---|---|---|
| **V21**, **V12**, **V6** | Sales in Belgium at 21, 12 or 6 % | Grids 03, 02, 01 and 54 |
| **V0** | Sales at 0 % (newspapers...) | Grid 00 |
| **VCC** | Works for a Belgian co-contractor: the customer pays the VAT | Grid 45 |
| **VEUG** | Goods to a company in another EU country | Grid 46, intra-Community listing |
| **VEUS** | Services to a company in another EU country | Grid 44, intra-Community listing |
| **VEUT** | Triangular sales (ABC) within the EU | Grid 46, intra-Community listing |
| **VEX** | Exports outside the EU | Grid 47 |
| **VX** | Exempt operations (article 44), outside the return | — |

The code is proposed from the customer's **VAT situation**, or its card's own **VAT code**. Credit
notes go to grids 48 (intra-Community) and 49 (others), their VAT to grid 64. Appendix A lists every
code.

> **Note.** On a printed invoice, the VAT detail carries the legal mention its codes need — an
> intra-Community reverse charge (art. 39bis, art. 21 § 2), the co-contractor's reverse charge (art. 20
> of Royal Decree no. 1), an export (art. 39), an exemption (art. 44) —, in the invoice's language.

### 6.4 Printing an invoice

On a saved invoice, click **Print** — or right-click it in the list and choose **Print**. Ledger hands
its data to **Letters**, which makes the invoice from its template and shows it: print it, or save it
again as `.docx` or `.odt`. See section 12.

![An invoice printed by Letters](images/invoice-printed.png)
*The same invoice printed: in Dutch, the customer's language — "FACTUUR".*

### 6.5 Credit notes

A credit note cancels all or part of an invoice. In the list, right-click the invoice and choose **Make
a credit note for it**: a credit note opens with the same customer and lines and a description
*Credit note for VEN 2026/0054*; change the amounts if only a part is credited, and save.

![The sales' context menu](images/sales-menu.png)
*Right-clicking a sale: open it, print it, make a credit note for it, delete it.*

You can also click **Credit note** at the top of the list for a credit note of your own. Once saved,
match it with its invoice in the customer's account (section 5.3) when it settles it.

### 6.6 Changing a sale

Open a sale, change it and save: its entry is posted again, with the same number. Its payment stays
matched as long as its total does not change. A sale cannot be changed once its VAT return is filed or
its year closed (section 3.4).

## 7. Purchases

### 7.1 The purchases

**Purchases** lists the purchase invoices and credit notes of the fiscal year, like the sales, with the
**supplier's own number** in place of the description.

![The purchases](images/purchases.png)
*The purchases of 2026: two still to pay.*

Its statuses are those of the sales, plus **Transfer sent**: the invoice is in a SEPA payment file
(section 8) and waits for the bank statement that pays it.

### 7.2 A purchase invoice

Click **New purchase**, or **Purchase** on the Overview, or **Documents ▸ Purchase Invoice**. You type it
as a sales invoice (section 6.2), with these differences:

- **Their number**: the supplier's invoice number (*F2026-4569*). Ledger warns you if the same supplier
  already has an invoice with that number this year: a purchase posted twice.
- **Communication**: the structured communication printed on the supplier's invoice, if any. Ledger
  checks its digits; the SEPA payment uses it.
- The **account** of each line: a charge (60 goods, 61 services, 62 salaries...) or, for equipment, a
  fixed asset (2x). The supplier's usual account is proposed.
- The **VAT code**: see section 7.3. With reverse charges, the entry shows both sides: the VAT due and
  the same VAT deductible.

![A purchase invoice: a car lease](images/purchase.png)
*A car lease: VAT code A21D50 — half of the VAT deductible (58,80), the other half added to the charge.*

### 7.3 The VAT codes of purchases

| Code | Use it for | VAT return |
|---|---|---|
| **A21**, **A12**, **A6** | Purchases in Belgium, VAT deductible | Grids 81/82/83 and 59 |
| **A0** | Purchases at 0 % or exempt | Grids 81/82/83 |
| **A21D50** | A company car: half of the VAT deductible | Grids 81/82/83 and 59 |
| **A21ND**, **A12ND** | VAT not deductible (restaurants, gifts...): added to the charge | Grids 81/82/83 |
| **AEU21**, **AEU12**, **AEU6** | Goods bought in another EU country | Grids 86, 55 and 59 |
| **AEUS21** | Services bought in another EU country | Grids 88, 55 and 59 |
| **ACC21**, **ACC12**, **ACC6** | Works from a Belgian co-contractor | Grids 87, 56 and 59 |
| **AWS21** | Services from outside the EU | Grids 87, 56 and 59 |
| **AIM21** | Imports, VAT deferred (licence ET 14000) | Grids 87, 57 and 59 |

The grid of the purchase itself — **81** goods, **82** services, **83** investments — follows the
line's account (its **Purchases** nature, section 4.5). Credit notes received go to grids 84 or 85, their
VAT to grid 63.

### 7.4 Purchase credit notes

Click **Credit note**, or right-click an invoice and choose **Make a credit note for it**. Type the
supplier's credit note number in **Their number**.

## 8. Paying your suppliers (SEPA)

Ledger writes the **SEPA credit transfer** file (the ISO 20022 *pain.001* format all Belgian banks
accept) that pays several invoices at once: upload it to your bank's site and confirm there.

1. In **Purchases**, click **Pay...** (or choose **Tools ▸ Pay Suppliers (SEPA)...**).
2. Choose the account you pay **From** (a bank journal with its IBAN) and the day the bank **Pays**.
3. Tick the invoices to pay. Those due within a week of that day are ticked for you. A supplier without
   an IBAN on its card shows **No IBAN** and cannot be ticked: complete its card. An invoice already in a
   file shows **In a file**.
4. Check the number of transfers and their total, and click **Make the file**.

![Paying the suppliers](images/pay-suppliers.png)
*Two invoices ticked: two transfers, 2.960,87 in all.*

The file is written in `SD:/docs/Payments`, named after the day (*SEPA 2026-09-28 123400.xml*). Each
transfer carries the supplier's name, address, IBAN and BIC, and its structured communication — else
the supplier's invoice number. The invoices then show **Transfer sent**, until the bank statement that
pays them is recorded (typed or imported): they are matched then.

## 9. Bank and cash

### 9.1 The statements

**Bank and cash** lists the bank and cash statements of the fiscal year: their number, date,
description, journal, the money **in** and **out**, and the **new balance**.

![The statements](images/bank.png)
*The bank statements of 2026, their movements in and out, and the balance after each.*

### 9.2 Typing a statement

Click **New statement** (or **Statement** on the Overview, or **Documents ▸ Bank Statement**).

1. **Journal**: the bank or cash journal. Its **old balance** — the balance after the last statement —
   is shown.
2. **Date**: the statement's date. **Description**: the number after the journal's last statement is
   proposed (*Statement 43* after *Statement 42*).
3. **New balance**: the balance your bank's statement prints. Optional, but recommended: Ledger then
   checks the movements against it and shows **Balanced**, or **Off by** the difference.
4. The **movements**, one per line:
   - **Party or account**: the customer or supplier who paid or was paid (type its name or code), or an
     account for anything else — bank charges 657200, the VAT paid to the State 451200, a salary 618000;
   - **Description**: the bank's communication;
   - **Amount**: positive for money **in**, negative for money **out** (`-238,37`).
5. When a movement has a party, its **open items** — its invoices not yet paid — show below. **Tick**
   those the movement pays: the amount follows them. Typing the amount first ticks the one item that
   is as much. And a **structured communication** typed in the description finds its invoice by
   itself.
6. **Save**. The items ticked are matched with their payment: the invoices show **Paid**.

![A saved statement](images/statement.png)
*A statement: each payment's invoice in "Pays"; below, the item a movement paid, ticked.*

A statement saved can be opened again: the **Pays** column shows the invoice each movement paid, and
the items list shows them ticked.

### 9.3 Importing CODA files

Belgian banks deliver their statements as **CODA** files (*Coded Statement of Account*, Febelfin's
standard). Download them from your online banking — often in the *Documents* or *CODA* section; some
banks send them to your accountant, who can pass them to you — and copy them to the SD card.

1. In **Bank and cash**, click **Import CODA** (or **Tools ▸ Import CODA...**) and choose the file.
2. Ledger reads its statements — a file can hold several —, finds each one's **journal by its IBAN**,
   and skips those already in your books.
3. Each statement opens, **already completed**:
   - a payment with a **structured communication** finds its invoice — a sale or a purchase —, which is
     ticked;
   - otherwise the counterparty is found by its **IBAN** (a card's) or its **name**, and its open item of
     the same amount is ticked (or all its items, when together they make the amount);
   - the **bank charges** go to 657200;
   - what is not found shows **To complete** in red: type its party or account.
4. The page's title line says what is left: *CODA: 1 movement to complete*. Complete it and click
   **Save**: the statement is posted and the next one of the file shows, until the last.

![A CODA statement imported](images/coda.png)
*The demo's CODA file imported: five movements found (their invoices ticked), one to complete.*

> **Tip.** Try it on the demo: **Import CODA** ▸ `demo-bank-statement.cod` ▸ **Open**. Janssens Pieter's
> 250,00 is not a known party: type an account (a sundry income, say) or make him a card with **+**.

If Ledger says **No bank journal has the account BE..**, type that IBAN in the bank journal
(**Settings ▸ Journals**) and import the file again. If it says **the bank's old balance is not the
books'**, a statement is missing between the last one in your books and this one: import it first.

### 9.4 Cash

A **cash statement** (**Documents ▸ Cash Statement**, or **New statement** with the cash journal chosen)
is typed like a bank statement: cash sales in, small purchases out, the day's or the month's movements.

## 10. Miscellaneous operations

**Misc. operations** keeps the entries that are neither invoices nor statements: depreciation,
salaries booked from your social secretariat's summary, accruals, corrections, opening balances, and
the VAT settlements Ledger posts itself (section 14.3).

![The miscellaneous operations](images/misc.png)
*The miscellaneous operations of 2026: the VAT settlements of Q1 and Q2.*

Click **New operation**:

1. **Journal**, **Date** and **Description** (*Depreciation of the equipment 2026*).
2. The lines: the **account**, the **party** when the account is a customers' or suppliers' one (a
   line on 400000 needs its customer), a **description**, the **debit** or the **credit**, and a VAT
   code when the line must count in the VAT return (a regularisation: codes R61, R62).
3. The title line shows the total **debit**, **credit** and the **difference**. **Balance it on this
   line** puts the difference on the line you are in.
4. **Save** when it **balances**.

![A new miscellaneous operation](images/misc-new.png)
*A depreciation: 630200 debited, 230009 credited, 1.425,00 each — balanced.*

Tick **Opening balances** for the entry that brings over the balances of your previous books (section
4.6); it is marked **Opening** in the list.

## 11. Quotes, orders and delivery notes

### 11.1 The commercial documents

**Quotes and orders** keeps the documents that come before an invoice. They are **not posted**: they
change neither the accounts nor the VAT.

| Document | What it is |
|---|---|
| **Quote** | A price offered to a customer, valid until a date (30 days by default) |
| **Order** | A customer's order (made from its quote, or typed) with its delivery date |
| **Delivery note** | What was delivered, to be signed by the customer |
| **Purchase order** | What you order from a supplier |

Each kind is numbered by year: *Quote 2026/0003*.

![The quotes and orders](images/quotes.png)
*The quotes, orders, delivery notes and purchase orders of the year, and their state.*

The filters show **All** or one kind; the **state** tells where each document stands:

| State | Meaning |
|---|---|
| **Draft** | Being prepared |
| **Sent** | Sent to the customer (or the supplier) |
| **Accepted** | The customer said yes |
| **Refused** | The customer said no |
| **Expired** | A quote past its validity, neither accepted nor refused |
| **Ordered**, **Delivered**, **Invoiced** | Done: what followed it |

### 11.2 A quote

Click **New quote** (or **Other...** for an order, a delivery note or a purchase order; or the
**Documents** menu).

![A quote](images/quote.png)
*A quote: three lines — quantity times unit price —, its VAT and total.*

1. The **customer**, the **date**, **Valid until** (a quote) or **Delivery** (the others), a
   **description** — the quote's title on paper (*Le packaging de Noël*) —, the **state** and the
   customer's **reference**.
2. The lines: a **description**, the **quantity** (`2,5` hours), the **unit price** excluding VAT and the
   **VAT code**; the **total** is computed.
3. **Save**, then **Print** to make it in Letters (section 12).

![A quote printed by Letters](images/quote-printed.png)
*The quote printed from its French template: the letterhead, the customer, the lines, the totals.*

### 11.3 The next step

**Next step** turns the document into the next one:

![The next step](images/quote-next.png)
*A quote's next steps: the order, a delivery note, the invoice; accepted, refused.*

- **Make the order** (a quote): an order with the same customer and lines; the quote is marked
  accepted.
- **Make a delivery note** (a quote or an order).
- **Make the invoice**: the sales invoice's page opens, filled — each line *2,5 x Design...* with its
  total, on the customer's usual account. Check it and **Save**: it is posted and the document is marked
  **Invoiced**.
- **Accepted** and **Refused** (a quote) record the customer's answer.

A document made from another says so on paper (*From Quote 2026/0001*). A **purchase order**'s next step
makes the purchase invoice, to be completed with the supplier's number.

## 12. Printing documents from templates

### 12.1 How it works

**Print** — on a quote, an order, a delivery note, a purchase order, a sales invoice or credit note —
writes the document's data and asks **Letters** to make the document from its **template**. Letters then
shows it: print it, or save it again as `.docx` or `.odt`.

The documents made are kept in `SD:/docs`, a folder per kind — **Quotes**, **Orders**, **Delivery
notes**, **Purchase orders**, **Invoices**, **Credit notes** —, named after their number and party
(*Quote 2026-0002 Chocolaterie Van Hove SRL.rtf*).

### 12.2 Languages

Each template exists in **French**, **Dutch** and **English**. A party's documents take the language of
its card, else the company's (its chart's). The words (*Facture*, *Factuur*, *Invoice*), the dates and
the legal mentions follow.

### 12.3 Your own templates

A template is an ordinary Letters document — `.rtf`, `.docx` or `.odt` — whose **merge fields** Ledger
fills. They are in `SD:/apps/ledger.app/templates`: the French ones in that folder, the Dutch ones in
`nl`, the English ones in `en`, one per kind: `quote`, `order`, `delivery`, `porder`, `invoice`,
`creditnote`.

![Settings, printing](images/settings-printing.png)
*The templates of each language: their own, or the French one used in their place.*

In **Settings ▸ Printing**, choose the language and the document and click **Edit in Letters**. Change
the look, the words, the conditions; add your logo (**Insert ▸ Image...** in Letters); move the fields.
When a language has no template of its own (**French one**), Ledger offers to make it from the French
one. **Open the folder** shows the templates in the File Viewer.

To add a field, use **Tools ▸ Mail Merge** in Letters: it lists all of Ledger's fields, with a sample's
values. A **table row** that holds line fields (**LineText**, **LineQty**...) is repeated for each line
of the document. Appendix B lists every field.

> **Tip.** Keep a copy of a template before you change it. If one is spoiled, the original ones come
> back with a fresh copy of the Onyx card's `apps/ledger.app/templates` folder.

## 13. Reports

### 13.1 Choosing a report

**Reports** shows a report on screen; **Letters** opens it as a document to print (A4, landscape when it
is wide, with its title and page numbers), **Spreadsheet** as a workbook, and **Save as...** writes it
as a Letters document (`.rtf`), a workbook (`.xlsx`) or a CSV file — in `SD:/docs/Reports` by default.

![Choosing a report](images/reports-list.png)
*The reports.*

Choose the **report**, then its **period**: **Year**, **Q1** to **Q4**, **Month** (the current one), or
dates typed in **From** and **to** (the balances: **At** a date). Double-click a line to open its document,
its account or its party.

### 13.2 The reports one by one

| Report | What it shows | Its options |
|---|---|---|
| **Journals** | Each document of the period with its entry's lines | A journal, or all |
| **General ledger** | Each account's lines, the balance brought forward and the running balance | A range of accounts; zero balances too |
| **Trial balance** | Each account's debits, credits and balance at a date | Zero balances too |
| **Balance sheet** | The assets and the liabilities, in the abbreviated scheme's headings | At a date |
| **Income statement** | The income and the charges, the result of the period | — |
| **Customers' balances**, **Suppliers' balances** | Each party's debits, credits, balance and overdue amount | At a date |
| **Receivables by age**, **Payables by age** | The open items by age: not due, 1–30, 31–60, 61–90, over 90 days | At a date |
| **A party's account** | A customer's or supplier's lines, with their due dates and matching | The party |
| **VAT detail** | The lines behind each grid of the VAT return | — |

![The general ledger](images/reports.png)
*The general ledger of 2026: each account's balance brought forward, lines and running balance.*

![The balance sheet](images/balance-sheet.png)
*The balance sheet at 31/12/2026, in the headings of the abbreviated scheme.*

![The income statement](images/income-statement.png)
*The income statement of 2026: turnover, charges by heading, the result.*

![Receivables by age](images/receivables.png)
*What the customers owe, by age.*

![A report in Letters](images/report-letters.png)
*The balance sheet opened in Letters, ready to print.*

## 14. VAT

### 14.1 The VAT page

**VAT** shows the VAT returns of a calendar year: its periods — quarters or months — as tiles, and the
chosen period's return as the form has it.

![The VAT returns](images/vat.png)
*Q3 2026: the return's grids, computed from the books; the checks find nothing wrong.*

Each period's state:

| State | Meaning |
|---|---|
| **Filed** | Marked filed (on that day): its VAT entries are locked |
| **Running** | The period is not over |
| **To file** | Over, its return due (the 20th of the next month) |
| **Late** | Its due date is past |
| **To come** | Not begun |

Click a tile — or use `←` and `→` — to see another period. The return is computed from the documents
of the period, in the form's sections: **II** the operations (sales, grids 00 to 49), **III** the
purchases (81 to 88), **IV** the VAT due (54 to 63, total **XX**), **V** the VAT deductible (59, 62, 64,
total **YY**), **VI** the balance: **71** due to the State, or **72** due by the State. Click a grid,
or **Detail**, to see the lines behind the grids (the **VAT detail** report of the period).

Below the form, **the checks Intervat makes** — a VAT due without its base, a credit note without an
operation... When they find something, check it before filing.

### 14.2 Filing the return

1. Check the grids (and their **Detail**).
2. Tick **Ask for the refund** if grid 72 shows an amount the State owes you and you want it paid back;
   **Ask for payment forms** if you want the paper forms.
3. Click **Intervat XML** and save the file (in `SD:/docs/VAT`, *VAT return 2026-Q3.xml*).
4. On **Intervat** (intervat.minfin.fgov.be), upload the file and send the return.
5. Answer **Yes** when Ledger asks whether to mark the period filed.

For monthly returns, the December one shows grid **91**, the advance paid in December: type it there.

### 14.3 Marking filed, the settlement

**Mark as filed** locks the period's VAT: its documents with VAT can no longer be changed, and the
return's grids are kept as filed. Ledger then offers to post the **settlement**: a miscellaneous
operation on the period's last day that moves the VAT due (451000) and the VAT deductible (411000) to
the **VAT current account** — 451200 what you pay the State, 411200 what it refunds you.

When you pay the VAT, record the payment in your bank statement on account **451200**.

**Reopen** unlocks a period filed by mistake. A return already sent must then be corrected on Intervat
too.

### 14.4 The listings

**Listings** makes the two other VAT files:

![The listings](images/vat-listings.png)
*The annual customer listing and the intra-Community listing of the period.*

- **Customer listing 2026 (XML)**: the annual list of your Belgian customers subject to VAT who bought
  250 EUR or more, due by March 31st of the next year. With no such customer, the listing is nil: say so
  in the year's last return — Ledger does it in that return's file.
- **Intra-community listing (XML)**: your intra-Community supplies and services of the period, per
  customer's VAT number. A customer without a valid EU VAT number is left out, and Ledger tells you.

Upload them on Intervat as well.

### 14.5 Without VAT returns

With the **small business franchise**, you file no periodic return; the customer listing is still due.
**Not subject to VAT**: no VAT file at all. The VAT page says so.

## 15. Closing the fiscal year

### 15.1 Before closing

- Record every document of the year: sales, purchases, all the bank statements up to the last day.
- File and mark filed the VAT returns of the year.
- Post the year-end entries with your accountant: depreciation, accruals and deferrals, provisions,
  stocks, taxes.
- Check the **Trial balance**, the **Balance sheet** and the **Income statement**, and give them to your
  accountant.

### 15.2 Closing

In **Settings ▸ Fiscal years**, choose the year and click **Close the year...** (or **Tools ▸ Close the
Fiscal Year...**).

![Closing the fiscal year](images/close-year.png)
*Closing 2026: its profit carried forward by an entry on its last day.*

Ledger posts the **appropriation** of the result on the year's last day — a profit: 693000 debited,
140000 credited; a loss: 141000 debited, 793000 credited —, **locks** the year's entries and adds the
next year if there is none. The balance sheet's accounts go on in the next year by themselves.

If your general meeting appropriates the result otherwise (a dividend, the legal reserve), post those
entries in the next year (or before closing, with accounts 69x / 79x: Ledger then appropriates only
what is left).

**Reopen the year** unlocks a closed year; its appropriation entry stays — delete it if the result
changes, and close the year again.

## 16. Files and backups

| Where | What |
|---|---|
| `SD:/docs/<company>.ledger` | The company's books: everything, in one file (you choose its name and place) |
| `<company>.ledger.bak` | The previous version of the books, kept at each change |
| `SD:/docs/Quotes`, `Orders`, `Delivery notes`, `Purchase orders`, `Invoices`, `Credit notes` | The documents printed |
| `SD:/docs/Reports` | The reports opened in Letters or the Spreadsheet |
| `SD:/docs/VAT` | The VAT returns and listings (XML) |
| `SD:/docs/Payments` | The SEPA payment files |
| `SD:/apps/ledger.app/templates` | The templates (`nl`, `en`: the Dutch and English ones) |
| `SD:/apps/ledger.app/last.txt` | The books opened last |
| `SD:/apps/ledger.app/merge.card`, `merge-lines.card`, `merge.job` | The data of the last document printed, for Letters |

**Back up** your `.ledger` file regularly — at least after each VAT return — to a USB stick or another
computer: it is your whole accounting. **File ▸ Save a Copy As...** writes a copy wherever you want.

The `.ledger` file is plain text (Latin-1): a `[company]` section, then `[years]`, `[journals]`,
`[accounts]`, `[parties]`, `[entries]`, `[returns]` and `[documents]`, a tab between the cells. It can
be read — and, in an emergency, mended — with any text editor. Keep a copy before touching it.

## 17. Questions and answers

**I cannot change an invoice: it says "Locked".** Its VAT return is filed or its year closed (section
3.4). Reopen the period (**VAT ▸ Reopen**) or the year only if the return or the accounts were not
sent yet; otherwise post a credit note or a correcting operation now.

**"Only the journal's last sales document can be deleted".** Sales invoices keep an unbroken
numbering. Make a credit note for the invoice (right-click ▸ **Make a credit note for it**).

**"The date is in no fiscal year".** Add the year in **Settings ▸ Fiscal years** (**Add the next
year**), or correct the date.

**"The fiscal year shown is closed: choose another one".** New documents go into the year shown in the
side bar: choose the current year there.

**An invoice paid still shows "Due" or "late".** Its payment was recorded without being matched. In the
customer's account, tick the invoice and the payment and click **Match** (section 5.3).

**The statement does not balance ("Off by...").** A movement is missing or mistyped: compare the lines
with the bank's statement. Leave the **New balance** empty to save it anyway.

**The CODA import says "No bank journal has the account".** Type the IBAN of that bank account in its
journal (**Settings ▸ Journals**).

**The VAT checks find something.** Open **Detail**, find the document, correct it (its VAT code, its
account's nature), and look again.

**A customer's VAT number shows "Its check digits are wrong".** Check it on the European VIES site;
Ledger keeps it anyway if you insist, but Intervat will refuse it in the listings.

**Letters shows "No template for this kind of document".** A template is missing in
`SD:/apps/ledger.app/templates`: copy it back from a fresh Onyx card.

**Where is my invoice as a file?** In `SD:/docs/Invoices`, named after its number and customer. Letters
can save it again as `.docx` or `.odt`.

**The dot at the foot of the side bar is red.** The books could not be written (the card full or
write-protected). Free some room, then **File ▸ Save a Copy As...**.

## 18. Glossary

| English | Français | Nederlands |
|---|---|---|
| Account | Compte | Rekening |
| Accountant | Expert-comptable | Accountant, boekhouder |
| Balance sheet | Bilan | Balans |
| Chart of accounts (PCMN) | Plan comptable minimum normalisé | Minimum algemeen rekeningenstelsel (MAR) |
| Credit note | Note de crédit | Creditnota |
| Customer | Client | Klant |
| Customer listing | Listing clients | Klantenlisting |
| Debit, credit | Débit, crédit | Debet, credit |
| Delivery note | Bon de livraison | Leveringsbon |
| Depreciation | Amortissement | Afschrijving |
| Due date | Échéance | Vervaldag |
| Entry | Écriture | Boeking |
| Fiscal year | Exercice comptable | Boekjaar |
| General ledger | Grand livre | Grootboek |
| Income statement | Compte de résultats | Resultatenrekening |
| Invoice | Facture | Factuur |
| Journal | Journal | Dagboek |
| Matching | Lettrage | Afpunten |
| Miscellaneous operation | Opération diverse (OD) | Diverse verrichting |
| Open item | Poste ouvert | Openstaande post |
| Order | Commande, bon de commande | Bestelling, bestelbon |
| Purchase | Achat | Aankoop |
| Quote | Devis, offre | Offerte |
| Reverse charge | Autoliquidation | Verlegging van heffing |
| Sale | Vente | Verkoop |
| Structured communication | Communication structurée | Gestructureerde mededeling |
| Supplier | Fournisseur | Leverancier |
| Trial balance | Balance des comptes | Proef- en saldibalans |
| VAT return | Déclaration TVA | Btw-aangifte |
| VAT number | Numéro de TVA | Btw-nummer |

## Appendix A. The VAT codes

| Code | Name | Rate | Deductible | Grids (invoice) | Grids (credit note) |
|---|---|---|---|---|---|
| V21 | Sales 21 % | 21 % | — | 03, 54 | 49, 64 |
| V12 | Sales 12 % | 12 % | — | 02, 54 | 49, 64 |
| V6 | Sales 6 % | 6 % | — | 01, 54 | 49, 64 |
| V0 | Sales 0 % | 0 % | — | 00 | 49 |
| VCC | Sales, VAT due by the Belgian co-contractor | — | — | 45 | 49 |
| VEUG | Intra-EU supplies of goods | — | — | 46 | 48 |
| VEUS | Intra-EU services | — | — | 44 | 48 |
| VEUT | Intra-EU triangular sales (ABC) | — | — | 46 | 48 |
| VEX | Exports outside the EU | — | — | 47 | 49 |
| VX | Exempt (art. 44), not in the return | — | — | — | — |
| A21 | Purchases 21 % | 21 % | 100 % | 81/82/83, 59 | 81/82/83, 85, 63 |
| A12 | Purchases 12 % | 12 % | 100 % | 81/82/83, 59 | 81/82/83, 85, 63 |
| A6 | Purchases 6 % | 6 % | 100 % | 81/82/83, 59 | 81/82/83, 85, 63 |
| A0 | Purchases 0 % or exempt | 0 % | — | 81/82/83 | 81/82/83, 85 |
| A21D50 | Purchases 21 %, half deductible (cars) | 21 % | 50 % | 81/82/83, 59 | 81/82/83, 85, 63 |
| A21ND | Purchases 21 %, not deductible | 21 % | 0 % | 81/82/83 | 81/82/83, 85 |
| A12ND | Purchases 12 %, not deductible | 12 % | 0 % | 81/82/83 | 81/82/83, 85 |
| AEU21 | Intra-EU acquisitions of goods 21 % | 21 % | 100 % | 81/82/83, 86, 55, 59 | 81/82/83, 86, 84 |
| AEU12 | Intra-EU acquisitions of goods 12 % | 12 % | 100 % | 81/82/83, 86, 55, 59 | 81/82/83, 86, 84 |
| AEU6 | Intra-EU acquisitions of goods 6 % | 6 % | 100 % | 81/82/83, 86, 55, 59 | 81/82/83, 86, 84 |
| AEUS21 | Intra-EU services received 21 % | 21 % | 100 % | 81/82/83, 88, 55, 59 | 81/82/83, 88, 84 |
| ACC21 | Belgian co-contractor (reverse charge) 21 % | 21 % | 100 % | 81/82/83, 87, 56, 59 | 81/82/83, 87, 85 |
| ACC12 | Belgian co-contractor (reverse charge) 12 % | 12 % | 100 % | 81/82/83, 87, 56, 59 | 81/82/83, 87, 85 |
| ACC6 | Belgian co-contractor (reverse charge) 6 % | 6 % | 100 % | 81/82/83, 87, 56, 59 | 81/82/83, 87, 85 |
| AWS21 | Services from outside the EU 21 % (reverse charge) | 21 % | 100 % | 81/82/83, 87, 56, 59 | 81/82/83, 87, 85 |
| AIM21 | Imports, VAT deferred (ET 14000) 21 % | 21 % | 100 % | 81/82/83, 87, 57, 59 | 81/82/83, 87, 85 |
| R61 | Regularisation for the State | — | — | 61 | 61 |
| R62 | Regularisation for the declarant | — | — | 62 | 62 |

Grid 81, 82 or 83 is the purchase account's nature: goods, services or investments (section 4.5).

## Appendix B. The merge fields

The fields a template can hold — typed in Letters as merge fields (**Tools ▸ Mail Merge**). The fields
ending in **Line**, **Address**, **Contact** and **Text** are made to be printed as they are: a label
and its value, in the document's language, or nothing at all when there is no value.

**The document**

| Field | What it holds |
|---|---|
| Kind | The document's kind, in its language: *Devis*, *Factuur*, *Invoice*... |
| Number | Its number: *2026/0002* |
| Date | Its date |
| Until | A quote's validity, an order's delivery date |
| UntilLine | *Valable jusqu'au 15/10/2026* |
| DueDate | An invoice's due date |
| Reference, ReferenceLine | The party's reference; with its label |
| Text | The description |
| Communication | An invoice's structured communication |
| Terms, TermsText | The payment terms in days; as printed (*30 jours*, *comptant*) |
| FromDocument, FromLine | The document it was made from; with its label |
| TotalNet, TotalVAT, Total | The totals |
| VATDetail | The VAT by rate, and the legal mentions of reverse charges and exemptions |
| FileName | The name of the document written |

**The company**

| Field | What it holds |
|---|---|
| CompanyName, CompanyLegal | The name, the legal form |
| CompanyStreet, CompanyZip, CompanyCity, CompanyCountry, CompanyAddress | The address (CompanyAddress: its lines) |
| CompanyVAT, CompanyVATLine | The VAT number; with its label |
| CompanyEmail, CompanyPhone, CompanyWeb, CompanyContact | The contacts (CompanyContact: all of them) |
| CompanyIBAN, CompanyBIC, CompanyBankLine | The bank; as a line |
| CompanyRegister, CompanyLegalLine | The register; the legal line — name and form, address, VAT number, register |

**The party**

| Field | What it holds |
|---|---|
| PartyName, PartyCode | The customer's or supplier's name, its code |
| PartyStreet, PartyZip, PartyCity, PartyCountry, PartyAddress | Its address |
| PartyVAT, PartyVATLine | Its VAT number; with its label |
| PartyEmail, PartyPhone | Its contacts |

**The lines** — in a table row, repeated for each line

| Field | What it holds |
|---|---|
| LineNo | The line's number |
| LineText | Its description |
| LineQty | The quantity |
| LinePrice | The unit price, excluding VAT |
| LineVAT | The VAT rate |
| LineTotal | The total excluding VAT |
| LineTax | The VAT |
| LineGross | The total including VAT |

## Appendix C. Keyboard reference

| Keys | Where | What they do |
|---|---|---|
| `Ctrl+N` | Everywhere | A new document of the page shown (a new card, a new account...) |
| `Ctrl+S` | A document | Save it |
| `Esc` | A document | Leave it (Ledger asks when something was typed) |
| `Ctrl+F` | A list | Go to the search box |
| `Ctrl+O` | Everywhere | Open another company's books |
| `Enter` | A list | Open the chosen row |
| `Delete` | A list | Delete the chosen row |
| `Space` | A party's account, a statement's items | Tick or untick the chosen line |
| `Tab`, `Enter` | A document's lines | The next cell; after the last one, a new line |
| `Shift+Tab` | A document's lines | The cell before |
| `↑`, `↓` | A document's lines | The line above, below |
| `F4`, `Alt+↓` | A party, account or VAT code cell | The list of what can be typed |
| `Ctrl+Delete` | A document's lines | Remove the line |
| `←`, `→` | The VAT page | The period before, after |
