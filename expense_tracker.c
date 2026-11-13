#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define MAX_RECORDS 1000
#define MAX_AMOUNT 100000000000LL
#define DATA_FILE "expenses.txt"

/* Money is stored in poisha to avoid rounding errors. */
typedef struct {
    int id;
    char date[11];
    char type;
    long long amount;
    char category[31];
    char note[81];
} Record;

Record records[MAX_RECORDS];
int count = 0;
int nextId = 1;

int readLine(const char *prompt, char *text, int size) {
    int ch;
    printf("%s", prompt);
    if (!fgets(text, size, stdin)) {
        printf("\nInput ended.\n");
        exit(0);
    }
    if (strchr(text, '\n')) {
        text[strcspn(text, "\n")] = '\0';
        return 1;
    }
    ch = getchar();
    if (ch == '\n' || ch == EOF) return 1;
    while ((ch = getchar()) != '\n' && ch != EOF) {}
    printf("Input is too long.\n");
    return 0;
}

int readNumber(const char *prompt, int low, int high) {
    char text[64], *end;
    long value;
    while (1) {
        if (!readLine(prompt, text, sizeof(text))) continue;
        errno = 0;
        value = strtol(text, &end, 10);
        if (end != text && *end == '\0' && !errno &&
            value >= low && value <= high) return (int)value;
        printf("Enter a whole number from %d to %d.\n", low, high);
    }
}

int validDate(const char *text) {
    int y, m, d, i;
    int days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (strlen(text) != 10 || text[4] != '-' || text[7] != '-') return 0;
    for (i = 0; i < 10; i++)
        if (i != 4 && i != 7 && !isdigit((unsigned char)text[i])) return 0;
    if (sscanf(text, "%d-%d-%d", &y, &m, &d) != 3) return 0;
    if (y < 1900 || y > 9999 || m < 1 || m > 12) return 0;
    if (y % 400 == 0 || (y % 4 == 0 && y % 100 != 0)) days[2] = 29;
    return d >= 1 && d <= days[m];
}

void readDate(char *date) {
    do {
        if (!readLine("Date (YYYY-MM-DD): ", date, 11)) continue;
        if (validDate(date)) return;
        printf("Enter a real date from year 1900 to 9999.\n");
    } while (1);
}

void readText(const char *prompt, char *text, int size) {
    int i, hasText, valid;
    while (1) {
        if (!readLine(prompt, text, size)) continue;
        hasText = 0;
        valid = 1;
        for (i = 0; text[i]; i++) {
            unsigned char c = (unsigned char)text[i];
            if (c < 32 || c > 126 || c == '|') valid = 0;
            if (!isspace(c)) hasText = 1;
        }
        if (valid && hasText) return;
        printf("Use English text. Do not leave it blank or use |.\n");
    }
}

long long readMoney(void) {
    char text[64];
    long long whole, fraction;
    int i, digits, valid;
    while (1) {
        if (!readLine("Amount in BDT (example 80.50): ", text, sizeof(text))) continue;
        whole = fraction = 0;
        i = digits = 0;
        valid = isdigit((unsigned char)text[0]);
        while (isdigit((unsigned char)text[i])) {
            whole = whole * 10 + text[i++] - '0';
            if (whole > MAX_AMOUNT / 100) { valid = 0; break; }
        }
        if (valid && text[i] == '.') {
            i++;
            while (isdigit((unsigned char)text[i]) && digits < 2) {
                fraction = fraction * 10 + text[i++] - '0';
                digits++;
            }
            if (!digits) valid = 0;
            if (digits == 1) fraction *= 10;
        }
        if (valid && !text[i] && whole * 100 + fraction > 0 &&
            whole * 100 + fraction <= MAX_AMOUNT) return whole * 100 + fraction;
        printf("Enter 0.01 to 1000000000.00, with at most two decimal places.\n");
    }
}

void printMoney(long long value) {
    if (value < 0) { printf("-"); value = -value; }
    printf("%lld.%02lld", value / 100, value % 100);
}

/* Write a new file first. Keep the old file if a write fails. */
int saveData(void) {
    int i, ok = 1;
    FILE *file = fopen("expenses.tmp", "w");
    if (!file) { printf("Cannot create the save file.\n"); return 0; }
    if (fprintf(file, "EXPENSE_TRACKER_V1 %d\n", nextId) < 0) ok = 0;
    for (i = 0; i < count; i++) {
        Record r = records[i];
        if (fprintf(file, "%d|%s|%c|%lld|%s|%s\n", r.id, r.date,
                    r.type, r.amount, r.category, r.note) < 0) ok = 0;
    }
    if (fclose(file) != 0) ok = 0;
    if (!ok) { remove("expenses.tmp"); printf("Save failed. No change was kept.\n"); return 0; }
    /* A backup also makes replacement work on Windows. */
    if (rename(DATA_FILE, "expenses.bak") != 0 && errno != ENOENT) {
        printf("Cannot back up the data file. No change was kept.\n");
        remove("expenses.tmp"); return 0;
    }
    if (rename("expenses.tmp", DATA_FILE) != 0) {
        rename("expenses.bak", DATA_FILE);
        printf("Cannot replace the data file. Check expenses.bak.\n");
        return 0;
    }
    return 1;
}

int loadData(void) {
    FILE *file = fopen(DATA_FILE, "r");
    char line[256], extra;
    int i, used;
    Record r;
    if (!file) {
        if (errno == ENOENT) {
            FILE *backup = fopen("expenses.bak", "r");
            if (backup) {
                fclose(backup);
                printf("Data file is missing. Restore expenses.bak before starting.\n");
                return 0;
            }
            return 1;
        }
        printf("Cannot read the data file.\n"); return 0;
    }
    if (!fgets(line, sizeof(line), file) ||
        sscanf(line, "EXPENSE_TRACKER_V1 %d %c", &nextId, &extra) != 1 ||
        nextId < 1 || nextId > MAX_RECORDS + 1) goto bad;
    while (fgets(line, sizeof(line), file)) {
        used = 0;
        if (count == MAX_RECORDS ||
            sscanf(line, "%d|%10[^|]|%c|%lld|%30[^|]|%80[^\n]%n",
                &r.id, r.date, &r.type, &r.amount, r.category, r.note, &used) != 6 ||
            (line[used] != '\n' && line[used] != '\0') ||
            r.id < 1 || r.id >= nextId || !validDate(r.date) ||
            (r.type != 'I' && r.type != 'E') || r.amount < 1 ||
            r.amount > MAX_AMOUNT || strchr(r.note, '|')) goto bad;
        for (i = 0; i < count; i++) if (records[i].id == r.id) goto bad;
        records[count++] = r;
    }
    if (ferror(file)) goto bad;
    fclose(file);
    return 1;
bad:
    fclose(file);
    printf("Data file is invalid. It was not changed. Restore a valid backup.\n");
    return 0;
}

void addRecord(char type) {
    Record r;
    if (count == MAX_RECORDS || nextId > MAX_RECORDS) {
        printf("This simple version allows 1000 records in total.\n"); return;
    }
    r.id = nextId;
    r.type = type;
    readDate(r.date);
    r.amount = readMoney();
    readText("Category (example Food, Travel, Salary): ", r.category, sizeof(r.category));
    readText("Note (use None if needed): ", r.note, sizeof(r.note));
    records[count++] = r;
    nextId++;
    if (saveData()) printf("Record saved. ID: %d\n", r.id);
    else { count--; nextId--; }
}

void showRecords(void) {
    int i;
    if (!count) { printf("No records found.\n"); return; }
    for (i = 0; i < count; i++) {
        Record r = records[i];
        printf("\nID: %d | Date: %s | %s\n", r.id, r.date,
               r.type == 'I' ? "Income" : "Expense");
        printf("Amount: BDT "); printMoney(r.amount);
        printf("\nCategory: %s\nNote: %s\n", r.category, r.note);
    }
}

void showSummary(int monthly) {
    long long income = 0, expense = 0;
    char date[11];
    int i;
    if (monthly) {
        printf("Enter any valid date in the month you want.\n");
        readDate(date);
    }
    for (i = 0; i < count; i++) {
        if (monthly && strncmp(records[i].date, date, 7)) continue;
        if (records[i].type == 'I') income += records[i].amount;
        else expense += records[i].amount;
    }
    printf("Income: BDT "); printMoney(income);
    printf("\nExpense: BDT "); printMoney(expense);
    printf(monthly ? "\nNet change: BDT " : "\nBalance: BDT ");
    printMoney(income - expense);
    printf("\n");
    if (!monthly && expense > income)
        printf("Expenses are higher than recorded income.\n");
}

void deleteRecord(void) {
    int id, i, j;
    Record old;
    if (!count) { printf("No records to delete.\n"); return; }
    id = readNumber("Record ID: ", 1, MAX_RECORDS);
    for (i = 0; i < count; i++) if (records[i].id == id) break;
    if (i == count) { printf("Record not found.\n"); return; }
    if (!readNumber("Delete this record? 1 = Yes, 0 = No: ", 0, 1)) return;
    old = records[i];
    for (j = i; j < count - 1; j++) records[j] = records[j + 1];
    count--;
    if (saveData()) printf("Record deleted.\n");
    else {
        for (j = count; j > i; j--) records[j] = records[j - 1];
        records[i] = old;
        count++;
    }
}

int main(void) {
    int choice;
    if (!loadData()) return 1;
    do {
        printf("\n=== PERSONAL EXPENSE TRACKER ===\n");
        printf("1. Add income\n2. Add expense\n3. View all records\n");
        printf("4. View balance\n5. Monthly summary\n6. Delete a record\n0. Exit\n");
        choice = readNumber("Choose: ", 0, 6);
        switch (choice) {
            case 1: addRecord('I'); break;
            case 2: addRecord('E'); break;
            case 3: showRecords(); break;
            case 4: showSummary(0); break;
            case 5: showSummary(1); break;
            case 6: deleteRecord(); break;
            case 0: printf("Goodbye.\n"); break;
        }
    } while (choice != 0);
    return 0;
}
