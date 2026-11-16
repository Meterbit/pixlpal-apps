/**
 * @file lnc_Mobile_Ui.h
 * @brief Loan & Savings Calculator's phone page (rendered by the Pixlpal phone app, format 2).
 *
 * Commands (loanCalculator.cpp):
 *   0 {"bank": "Lagoon Bank"}            1 {"currency": "KSh"}  written before amounts (empty: none)
 *   2 {"loan0" ... "loan2": "Personal loan 18.5% 5000000 60"}  a loan: its name, rate a year, largest amount, longest
 *                                        term in months (empty removes it; one stays)
 *   3 {"savings": "6.5% 100000"}         the savings rate a year, and the largest monthly deposit
 *   4 {"note": "Illustration only..."}   the line along the bottom
 */
#ifndef LNC_MOBILE_UI_H
#define LNC_MOBILE_UI_H

static const char LNC_UI_MANIFEST[] = R"json({
  "name": "Loan & Savings Calculator",
  "about": "A calculator customers use with the knob while they wait: choose a loan, an amount and a term, and see the monthly payment, the interest and the total to repay; or a monthly saving and see what it grows to. The bank sets its products and rates on the phone.",
  "author": "Meterbit Cyb",
  "version": "v1.0.0"
})json";

static const char LNC_UI_API[] = R"json({
  "widgets": [
    { "type": "image", "src": "https://raw.githubusercontent.com/Meterbit/pixlpal-apps/main/catalogue/icons/com.meterbit.loan-calculator.png", "height": 64, "alt": "Loan & Savings Calculator" },
    { "type": "statusPanel", "title": "On the Screen Now", "source": "calc" },
    {
      "type": "section",
      "id": "loans",
      "title": "Loans",
      "widgets": [
        { "type": "textField", "title": "Loan 1", "emptyHint": "Name, rate, largest amount, longest months: Personal loan 18.5% 5000000 60", "filledHint": "Change it", "command": { "app_command": 2, "args": ["loan0"] } },
        { "type": "textField", "title": "Loan 2", "emptyHint": "Car loan 15% 10000000 48", "filledHint": "Change it (empty removes it)", "command": { "app_command": 2, "args": ["loan1"] } },
        { "type": "textField", "title": "Loan 3", "emptyHint": "Mortgage 12% 80000000 240", "filledHint": "Change it (empty removes it)", "command": { "app_command": 2, "args": ["loan2"] } }
      ]
    },
    {
      "type": "section",
      "id": "setup",
      "title": "Savings and the Screen",
      "collapsed": true,
      "widgets": [
        { "type": "textField", "title": "Savings", "emptyHint": "Rate a year and largest monthly deposit: 6.5% 100000", "filledHint": "Change it", "command": { "app_command": 3, "args": ["savings"] } },
        { "type": "textField", "title": "Bank or Branch", "emptyHint": "e.g. Lagoon Bank", "filledHint": "Change the name", "command": { "app_command": 0, "args": ["bank"] } },
        { "type": "textField", "title": "Currency", "emptyHint": "Written before amounts: KSh, $, N", "filledHint": "Change it (empty: none)", "command": { "app_command": 1, "args": ["currency"] } },
        { "type": "textField", "title": "Note Along the Bottom", "emptyHint": "Illustration only: ask us for a quote.", "filledHint": "Change the note", "command": { "app_command": 4, "args": ["note"] } }
      ]
    },
    {
      "type": "richTextBlock",
      "title": "On Pixlpal",
      "blocks": [
        { "type": "bullet", "text": "Turn the knob to change the field in the box; click for the next field; double click to switch between loans and savings." },
        { "type": "bullet", "text": "Two minutes after the last turn it goes back to the first loan, ready for the next customer." },
        { "type": "note", "text": "Loans are worked out as equal monthly payments at the rate given (interest monthly, a twelfth of the yearly rate). Savings: the deposit at each month's end, interest added monthly. Fees and taxes are not included." }
      ]
    }
  ],
  "toasts": [
    { "when": { "app_command": 1, "response": 0 }, "text": "Up to 5 letters or signs" },
    { "when": { "app_command": 2, "response": 0 }, "text": "Write the name, rate, largest amount and months: Personal loan 18.5% 5000000 60" },
    { "when": { "app_command": 3, "response": 0 }, "text": "Write the rate and the largest deposit: 6.5% 100000" }
  ]
})json";

#endif
