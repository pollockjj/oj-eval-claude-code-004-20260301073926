#include <iostream>
#include <fstream>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cassert>

// ============================================================
// File Storage: Block Linked List for indexing
// ============================================================

template <int KEY_LEN = 65>
struct IndexEntry {
    char key[KEY_LEN];
    int value;
    IndexEntry() : value(0) { memset(key, 0, sizeof(key)); }
    IndexEntry(const std::string &k, int v) : value(v) {
        memset(key, 0, sizeof(key));
        strncpy(key, k.c_str(), KEY_LEN - 1);
    }
    bool operator<(const IndexEntry &o) const {
        int c = strcmp(key, o.key);
        if (c != 0) return c < 0;
        return value < o.value;
    }
    bool operator==(const IndexEntry &o) const {
        return strcmp(key, o.key) == 0 && value == o.value;
    }
    bool operator<=(const IndexEntry &o) const {
        return *this < o || *this == o;
    }
    bool operator>(const IndexEntry &o) const {
        return !(*this <= o);
    }
};

static const int BLOCK_SIZE = 300;

template <int KEY_LEN = 65>
struct Block {
    int cnt;
    int next; // file position of next block, 0 means no next
    IndexEntry<KEY_LEN> data[BLOCK_SIZE + 1]; // +1 for temporary overflow
    Block() : cnt(0), next(0) {}
};

template <int KEY_LEN = 65>
class BlockList {
    std::fstream file;
    std::string filename;
    int head_pos; // position of first block in file
    int file_end; // end of file for new allocation

    struct FileHeader {
        int head_pos;
        int file_end;
    };

    void read_header() {
        file.seekg(0);
        FileHeader h;
        file.read(reinterpret_cast<char *>(&h), sizeof(h));
        head_pos = h.head_pos;
        file_end = h.file_end;
    }

    void write_header() {
        file.seekp(0);
        FileHeader h{head_pos, file_end};
        file.write(reinterpret_cast<const char *>(&h), sizeof(h));
        file.flush();
    }

    void read_block(int pos, Block<KEY_LEN> &blk) {
        file.seekg(pos);
        file.read(reinterpret_cast<char *>(&blk), sizeof(blk));
    }

    void write_block(int pos, const Block<KEY_LEN> &blk) {
        file.seekp(pos);
        file.write(reinterpret_cast<const char *>(&blk), sizeof(blk));
        file.flush();
    }

    int alloc_block() {
        int pos = file_end;
        file_end += sizeof(Block<KEY_LEN>);
        write_header();
        return pos;
    }

public:
    BlockList() : head_pos(0), file_end(0) {}

    void init(const std::string &fname) {
        filename = fname;
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            // Create new file
            file.open(filename, std::ios::out | std::ios::binary);
            file.close();
            file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
            head_pos = 0;
            file_end = sizeof(FileHeader);
            write_header();
        } else {
            read_header();
        }
    }

    void insert(const std::string &key, int value) {
        IndexEntry<KEY_LEN> entry(key, value);
        if (head_pos == 0) {
            // Empty list, create first block
            Block<KEY_LEN> blk;
            blk.data[0] = entry;
            blk.cnt = 1;
            blk.next = 0;
            head_pos = alloc_block();
            write_block(head_pos, blk);
            write_header();
            return;
        }

        // Find the right block
        int cur_pos = head_pos;
        int prev_pos = 0;
        Block<KEY_LEN> cur;

        while (cur_pos != 0) {
            read_block(cur_pos, cur);
            if (cur.next == 0) break; // last block
            if (cur.cnt > 0 && entry < cur.data[cur.cnt - 1]) break; // belongs here
            // Check if entry equals the last element
            if (cur.cnt > 0 && entry == cur.data[cur.cnt - 1]) break;
            prev_pos = cur_pos;
            cur_pos = cur.next;
        }

        // Insert into cur block
        // Find position using binary search
        int lo = 0, hi = cur.cnt;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (cur.data[mid] < entry) lo = mid + 1;
            else hi = mid;
        }
        // Shift elements right
        for (int i = cur.cnt; i > lo; --i) {
            cur.data[i] = cur.data[i - 1];
        }
        cur.data[lo] = entry;
        cur.cnt++;

        // Check if need to split
        if (cur.cnt > BLOCK_SIZE) {
            // Split
            Block<KEY_LEN> new_blk;
            int mid = cur.cnt / 2;
            new_blk.cnt = cur.cnt - mid;
            for (int i = 0; i < new_blk.cnt; ++i) {
                new_blk.data[i] = cur.data[mid + i];
            }
            cur.cnt = mid;
            new_blk.next = cur.next;
            int new_pos = alloc_block();
            cur.next = new_pos;
            write_block(new_pos, new_blk);
        }
        write_block(cur_pos, cur);
    }

    void remove(const std::string &key, int value) {
        IndexEntry<KEY_LEN> entry(key, value);
        if (head_pos == 0) return;

        int cur_pos = head_pos;
        int prev_pos = 0;
        Block<KEY_LEN> cur;

        while (cur_pos != 0) {
            read_block(cur_pos, cur);
            // Check if entry could be in this block
            if (cur.cnt > 0 && (entry <= cur.data[cur.cnt - 1] || cur.next == 0)) {
                // Search in this block
                int lo = 0, hi = cur.cnt;
                while (lo < hi) {
                    int mid = (lo + hi) / 2;
                    if (cur.data[mid] < entry) lo = mid + 1;
                    else hi = mid;
                }
                if (lo < cur.cnt && cur.data[lo] == entry) {
                    // Found, remove it
                    for (int i = lo; i < cur.cnt - 1; ++i) {
                        cur.data[i] = cur.data[i + 1];
                    }
                    cur.cnt--;
                    if (cur.cnt == 0 && prev_pos != 0) {
                        // Remove this block by linking prev to next
                        Block<KEY_LEN> prev;
                        read_block(prev_pos, prev);
                        prev.next = cur.next;
                        write_block(prev_pos, prev);
                    } else if (cur.cnt == 0 && prev_pos == 0) {
                        // This is head block
                        head_pos = cur.next;
                        write_header();
                    } else {
                        // Try merge with next
                        if (cur.next != 0) {
                            Block<KEY_LEN> nxt;
                            read_block(cur.next, nxt);
                            if (cur.cnt + nxt.cnt <= BLOCK_SIZE) {
                                for (int i = 0; i < nxt.cnt; ++i) {
                                    cur.data[cur.cnt + i] = nxt.data[i];
                                }
                                cur.cnt += nxt.cnt;
                                cur.next = nxt.next;
                            }
                        }
                        write_block(cur_pos, cur);
                    }
                    return;
                }
                // Not found in this block but entry <= last element
                // Means not in the list
                if (entry <= cur.data[cur.cnt - 1]) return;
            }
            prev_pos = cur_pos;
            cur_pos = cur.next;
        }
    }

    // Find all values with given key
    std::vector<int> find(const std::string &key) {
        std::vector<int> result;
        if (head_pos == 0) return result;

        IndexEntry<KEY_LEN> lo_entry(key, 0); // smallest possible with this key

        int cur_pos = head_pos;
        Block<KEY_LEN> cur;

        while (cur_pos != 0) {
            read_block(cur_pos, cur);
            if (cur.cnt > 0 && lo_entry <= cur.data[cur.cnt - 1]) {
                // This block may contain matching entries
                for (int i = 0; i < cur.cnt; ++i) {
                    if (strcmp(cur.data[i].key, key.c_str()) == 0) {
                        result.push_back(cur.data[i].value);
                    } else if (strcmp(cur.data[i].key, key.c_str()) > 0) {
                        return result;
                    }
                }
            }
            cur_pos = cur.next;
        }
        return result;
    }

    // Check if a specific key-value pair exists
    bool exists(const std::string &key, int value) {
        IndexEntry<KEY_LEN> entry(key, value);
        if (head_pos == 0) return false;

        int cur_pos = head_pos;
        Block<KEY_LEN> cur;

        while (cur_pos != 0) {
            read_block(cur_pos, cur);
            if (cur.cnt > 0 && entry <= cur.data[cur.cnt - 1]) {
                int lo = 0, hi = cur.cnt;
                while (lo < hi) {
                    int mid = (lo + hi) / 2;
                    if (cur.data[mid] < entry) lo = mid + 1;
                    else hi = mid;
                }
                if (lo < cur.cnt && cur.data[lo] == entry) return true;
                if (entry <= cur.data[cur.cnt - 1]) return false;
            }
            cur_pos = cur.next;
        }
        return false;
    }

    // Get all entries (for show all)
    std::vector<int> get_all() {
        std::vector<int> result;
        if (head_pos == 0) return result;

        int cur_pos = head_pos;
        Block<KEY_LEN> cur;

        while (cur_pos != 0) {
            read_block(cur_pos, cur);
            for (int i = 0; i < cur.cnt; ++i) {
                result.push_back(cur.data[i].value);
            }
            cur_pos = cur.next;
        }
        return result;
    }
};


// ============================================================
// Data structures for accounts and books
// ============================================================

struct Account {
    char userid[31];
    char password[31];
    char username[31];
    int privilege;
    Account() : privilege(0) {
        memset(userid, 0, sizeof(userid));
        memset(password, 0, sizeof(password));
        memset(username, 0, sizeof(username));
    }
};

struct Book {
    char isbn[21];
    char name[61];
    char author[61];
    char keyword[61];
    double price;
    int stock;
    Book() : price(0.0), stock(0) {
        memset(isbn, 0, sizeof(isbn));
        memset(name, 0, sizeof(name));
        memset(author, 0, sizeof(author));
        memset(keyword, 0, sizeof(keyword));
    }
};

// ============================================================
// File-based storage for accounts and books (simple sequential file with free list)
// ============================================================

template <typename T>
class FileStorage {
    std::fstream file;
    std::string filename;
    int count; // total number of records
    int file_end;

    struct Header {
        int count;
        int file_end;
    };

    void read_header() {
        file.seekg(0);
        Header h;
        file.read(reinterpret_cast<char *>(&h), sizeof(h));
        count = h.count;
        file_end = h.file_end;
    }

    void write_header() {
        file.seekp(0);
        Header h{count, file_end};
        file.write(reinterpret_cast<const char *>(&h), sizeof(h));
        file.flush();
    }

public:
    FileStorage() : count(0), file_end(0) {}

    void init(const std::string &fname) {
        filename = fname;
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            file.open(filename, std::ios::out | std::ios::binary);
            file.close();
            file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
            count = 0;
            file_end = sizeof(Header);
            write_header();
        } else {
            read_header();
        }
    }

    // Returns the position (offset) where the record was written
    int add(const T &record) {
        int pos = file_end;
        file.seekp(pos);
        file.write(reinterpret_cast<const char *>(&record), sizeof(T));
        file.flush();
        count++;
        file_end = pos + sizeof(T);
        write_header();
        return pos;
    }

    void read(int pos, T &record) {
        file.seekg(pos);
        file.read(reinterpret_cast<char *>(&record), sizeof(T));
    }

    void write(int pos, const T &record) {
        file.seekp(pos);
        file.write(reinterpret_cast<const char *>(&record), sizeof(T));
        file.flush();
    }

    int get_count() { return count; }
};

// ============================================================
// Finance log
// ============================================================

struct FinanceRecord {
    double income;   // positive for buy
    double expense;  // positive for import
};

class FinanceLog {
    std::fstream file;
    std::string filename;
    int count;

    struct Header {
        int count;
    };

    void read_header() {
        file.seekg(0);
        Header h;
        file.read(reinterpret_cast<char *>(&h), sizeof(h));
        count = h.count;
    }

    void write_header() {
        file.seekp(0);
        Header h{count};
        file.write(reinterpret_cast<const char *>(&h), sizeof(h));
        file.flush();
    }

public:
    FinanceLog() : count(0) {}

    void init(const std::string &fname) {
        filename = fname;
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            file.open(filename, std::ios::out | std::ios::binary);
            file.close();
            file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
            count = 0;
            write_header();
        } else {
            read_header();
        }
    }

    void add(double income, double expense) {
        FinanceRecord rec{income, expense};
        file.seekp(sizeof(Header) + count * sizeof(FinanceRecord));
        file.write(reinterpret_cast<const char *>(&rec), sizeof(rec));
        file.flush();
        count++;
        write_header();
    }

    int get_count() { return count; }

    // Get last 'n' records' totals
    void query(int n, double &total_income, double &total_expense) {
        total_income = 0;
        total_expense = 0;
        int start = count - n;
        for (int i = start; i < count; ++i) {
            FinanceRecord rec;
            file.seekg(sizeof(Header) + i * sizeof(FinanceRecord));
            file.read(reinterpret_cast<char *>(&rec), sizeof(rec));
            total_income += rec.income;
            total_expense += rec.expense;
        }
    }
};

// ============================================================
// Log system
// ============================================================

struct LogEntry {
    char description[300];
};

class LogSystem {
    std::fstream file;
    std::string filename;
    int count;

    struct Header {
        int count;
    };

    void read_header() {
        file.seekg(0);
        Header h;
        file.read(reinterpret_cast<char *>(&h), sizeof(h));
        count = h.count;
    }

    void write_header() {
        file.seekp(0);
        Header h{count};
        file.write(reinterpret_cast<const char *>(&h), sizeof(h));
        file.flush();
    }

public:
    LogSystem() : count(0) {}

    void init(const std::string &fname) {
        filename = fname;
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            file.open(filename, std::ios::out | std::ios::binary);
            file.close();
            file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
            count = 0;
            write_header();
        } else {
            read_header();
        }
    }

    void add(const std::string &desc) {
        LogEntry entry;
        memset(entry.description, 0, sizeof(entry.description));
        strncpy(entry.description, desc.c_str(), sizeof(entry.description) - 1);
        file.seekp(sizeof(Header) + count * sizeof(LogEntry));
        file.write(reinterpret_cast<const char *>(&entry), sizeof(entry));
        file.flush();
        count++;
        write_header();
    }

    void print_all() {
        for (int i = 0; i < count; ++i) {
            LogEntry entry;
            file.seekg(sizeof(Header) + i * sizeof(LogEntry));
            file.read(reinterpret_cast<char *>(&entry), sizeof(entry));
            std::cout << entry.description << "\n";
        }
    }

    int get_count() { return count; }
};

// ============================================================
// Employee log
// ============================================================

struct EmployeeLogEntry {
    char userid[31];
    char description[270];
};

class EmployeeLog {
    std::fstream file;
    std::string filename;
    int count;

    struct Header {
        int count;
    };

    void read_header() {
        file.seekg(0);
        Header h;
        file.read(reinterpret_cast<char *>(&h), sizeof(h));
        count = h.count;
    }

    void write_header() {
        file.seekp(0);
        Header h{count};
        file.write(reinterpret_cast<const char *>(&h), sizeof(h));
        file.flush();
    }

public:
    EmployeeLog() : count(0) {}

    void init(const std::string &fname) {
        filename = fname;
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            file.open(filename, std::ios::out | std::ios::binary);
            file.close();
            file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
            count = 0;
            write_header();
        } else {
            read_header();
        }
    }

    void add(const std::string &userid, const std::string &desc) {
        EmployeeLogEntry entry;
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.userid, userid.c_str(), sizeof(entry.userid) - 1);
        strncpy(entry.description, desc.c_str(), sizeof(entry.description) - 1);
        file.seekp(sizeof(Header) + count * sizeof(EmployeeLogEntry));
        file.write(reinterpret_cast<const char *>(&entry), sizeof(entry));
        file.flush();
        count++;
        write_header();
    }

    void print_all() {
        std::cout << "Employee Work Report\n";
        for (int i = 0; i < count; ++i) {
            EmployeeLogEntry entry;
            file.seekg(sizeof(Header) + i * sizeof(EmployeeLogEntry));
            file.read(reinterpret_cast<char *>(&entry), sizeof(entry));
            std::cout << entry.userid << ": " << entry.description << "\n";
        }
    }
};

// ============================================================
// Global variables
// ============================================================

FileStorage<Account> account_store;
FileStorage<Book> book_store;
BlockList<31> userid_index;   // userid -> account position
BlockList<21> isbn_index;     // isbn -> book position
BlockList<61> name_index;     // name -> book position
BlockList<61> author_index;   // author -> book position
BlockList<61> keyword_index;  // keyword -> book position
FinanceLog finance_log;
LogSystem log_system;
EmployeeLog employee_log;

// Login stack
struct LoginInfo {
    std::string userid;
    int privilege;
    int selected_book_pos; // -1 means no book selected
    int account_pos;
};
std::vector<LoginInfo> login_stack;

// Track logged-in count per user
// We need to track how many times each user is logged in
// Use a simple approach: count from login_stack
int count_logged_in(const std::string &userid) {
    int cnt = 0;
    for (auto &li : login_stack) {
        if (li.userid == userid) cnt++;
    }
    return cnt;
}

int current_privilege() {
    if (login_stack.empty()) return 0;
    return login_stack.back().privilege;
}

std::string current_userid() {
    if (login_stack.empty()) return "";
    return login_stack.back().userid;
}

// ============================================================
// Validation functions
// ============================================================

bool is_valid_userid_char(char c) {
    return isalnum(c) || c == '_';
}

bool is_valid_userid(const std::string &s) {
    if (s.empty() || s.length() > 30) return false;
    for (char c : s) {
        if (!is_valid_userid_char(c)) return false;
    }
    return true;
}

bool is_valid_password(const std::string &s) {
    return is_valid_userid(s); // Same rules
}

bool is_valid_username(const std::string &s) {
    if (s.empty() || s.length() > 30) return false;
    for (unsigned char c : s) {
        if (c <= 32 || c >= 127) return false; // invisible chars
    }
    return true;
}

bool is_valid_privilege(const std::string &s) {
    if (s.length() != 1) return false;
    return s[0] == '1' || s[0] == '3' || s[0] == '7';
}

bool is_valid_isbn(const std::string &s) {
    if (s.empty() || s.length() > 20) return false;
    for (unsigned char c : s) {
        if (c <= 32 || c >= 127) return false;
    }
    return true;
}

bool is_valid_bookname(const std::string &s) {
    if (s.empty() || s.length() > 60) return false;
    for (unsigned char c : s) {
        if (c <= 32 || c >= 127 || c == '"') return false;
    }
    return true;
}

bool is_valid_author(const std::string &s) {
    return is_valid_bookname(s); // same rules
}

bool is_valid_keyword(const std::string &s) {
    if (s.empty() || s.length() > 60) return false;
    for (unsigned char c : s) {
        if (c <= 32 || c >= 127 || c == '"') return false;
    }
    return true;
}

bool is_valid_quantity(const std::string &s) {
    if (s.empty() || s.length() > 10) return false;
    for (char c : s) {
        if (!isdigit(c)) return false;
    }
    // No leading zeros allowed? Actually the problem doesn't say
    // But value must not exceed 2147483647
    if (s.length() > 10) return false;
    long long v = 0;
    for (char c : s) {
        v = v * 10 + (c - '0');
        if (v > 2147483647LL) return false;
    }
    return true;
}

bool is_positive_quantity(const std::string &s) {
    if (!is_valid_quantity(s)) return false;
    long long v = 0;
    for (char c : s) v = v * 10 + (c - '0');
    return v > 0;
}

bool is_valid_price(const std::string &s) {
    if (s.empty() || s.length() > 13) return false;
    int dot_count = 0;
    for (char c : s) {
        if (c == '.') dot_count++;
        else if (!isdigit(c)) return false;
    }
    if (dot_count > 1) return false;
    // Must be a valid number
    if (s == ".") return false;
    return true;
}

bool is_positive_price(const std::string &s) {
    if (!is_valid_price(s)) return false;
    double v = std::stod(s);
    return v > 0;
}

bool is_valid_count(const std::string &s) {
    return is_valid_quantity(s);
}

// ============================================================
// Helper: split keywords by '|'
// ============================================================

std::vector<std::string> split_keywords(const std::string &s) {
    std::vector<std::string> result;
    std::string cur;
    for (char c : s) {
        if (c == '|') {
            result.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    result.push_back(cur);
    return result;
}

// Check for duplicate keywords
bool has_duplicate_keywords(const std::vector<std::string> &kws) {
    for (int i = 0; i < (int)kws.size(); ++i) {
        for (int j = i + 1; j < (int)kws.size(); ++j) {
            if (kws[i] == kws[j]) return true;
        }
    }
    return false;
}

// ============================================================
// Helper: find account position by userid
// ============================================================

int find_account_pos(const std::string &userid) {
    auto results = userid_index.find(userid);
    if (results.empty()) return -1;
    return results[0];
}

// ============================================================
// Helper: find book position by isbn
// ============================================================

int find_book_pos(const std::string &isbn) {
    auto results = isbn_index.find(isbn);
    if (results.empty()) return -1;
    return results[0];
}

// ============================================================
// Command parsing
// ============================================================

std::vector<std::string> tokenize(const std::string &line) {
    std::vector<std::string> tokens;
    std::string token;
    bool in_token = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == ' ') {
            if (in_token) {
                tokens.push_back(token);
                token.clear();
                in_token = false;
            }
        } else {
            token += line[i];
            in_token = true;
        }
    }
    if (in_token) tokens.push_back(token);
    return tokens;
}

// ============================================================
// Command handlers
// ============================================================

void cmd_su(const std::vector<std::string> &tokens) {
    // su [UserID] ([Password])?
    if (tokens.size() < 2 || tokens.size() > 3) {
        std::cout << "Invalid\n";
        return;
    }
    std::string userid = tokens[1];
    if (!is_valid_userid(userid)) {
        std::cout << "Invalid\n";
        return;
    }

    int pos = find_account_pos(userid);
    if (pos == -1) {
        std::cout << "Invalid\n";
        return;
    }

    Account acc;
    account_store.read(pos, acc);

    if (tokens.size() == 3) {
        // Password provided
        std::string password = tokens[2];
        if (!is_valid_password(password)) {
            std::cout << "Invalid\n";
            return;
        }
        if (strcmp(acc.password, password.c_str()) != 0) {
            std::cout << "Invalid\n";
            return;
        }
    } else {
        // No password - current privilege must be higher
        if (current_privilege() <= acc.privilege) {
            std::cout << "Invalid\n";
            return;
        }
    }

    LoginInfo li;
    li.userid = userid;
    li.privilege = acc.privilege;
    li.selected_book_pos = -1;
    li.account_pos = pos;
    login_stack.push_back(li);
}

void cmd_logout(const std::vector<std::string> &tokens) {
    if (tokens.size() != 1) {
        std::cout << "Invalid\n";
        return;
    }
    if (login_stack.empty()) {
        std::cout << "Invalid\n";
        return;
    }
    login_stack.pop_back();
}

void cmd_register(const std::vector<std::string> &tokens) {
    // register [UserID] [Password] [Username]
    if (tokens.size() != 4) {
        std::cout << "Invalid\n";
        return;
    }
    std::string userid = tokens[1];
    std::string password = tokens[2];
    std::string username = tokens[3];

    if (!is_valid_userid(userid) || !is_valid_password(password) || !is_valid_username(username)) {
        std::cout << "Invalid\n";
        return;
    }

    if (find_account_pos(userid) != -1) {
        std::cout << "Invalid\n";
        return;
    }

    Account acc;
    strncpy(acc.userid, userid.c_str(), 30);
    strncpy(acc.password, password.c_str(), 30);
    strncpy(acc.username, username.c_str(), 30);
    acc.privilege = 1;
    int pos = account_store.add(acc);
    userid_index.insert(userid, pos);

    log_system.add("register " + userid + " " + username);
}

void cmd_passwd(const std::vector<std::string> &tokens) {
    // passwd [UserID] ([CurrentPassword])? [NewPassword]
    if (tokens.size() < 3 || tokens.size() > 4) {
        std::cout << "Invalid\n";
        return;
    }
    if (current_privilege() < 1) {
        std::cout << "Invalid\n";
        return;
    }

    std::string userid = tokens[1];
    if (!is_valid_userid(userid)) {
        std::cout << "Invalid\n";
        return;
    }

    int pos = find_account_pos(userid);
    if (pos == -1) {
        std::cout << "Invalid\n";
        return;
    }

    Account acc;
    account_store.read(pos, acc);

    if (tokens.size() == 4) {
        std::string cur_pwd = tokens[2];
        std::string new_pwd = tokens[3];
        if (!is_valid_password(cur_pwd) || !is_valid_password(new_pwd)) {
            std::cout << "Invalid\n";
            return;
        }
        if (strcmp(acc.password, cur_pwd.c_str()) != 0) {
            std::cout << "Invalid\n";
            return;
        }
        strncpy(acc.password, new_pwd.c_str(), 30);
        acc.password[30] = 0;
        account_store.write(pos, acc);
    } else {
        // tokens.size() == 3, privilege must be 7
        if (current_privilege() != 7) {
            std::cout << "Invalid\n";
            return;
        }
        std::string new_pwd = tokens[2];
        if (!is_valid_password(new_pwd)) {
            std::cout << "Invalid\n";
            return;
        }
        strncpy(acc.password, new_pwd.c_str(), 30);
        acc.password[30] = 0;
        account_store.write(pos, acc);
    }

    log_system.add(current_userid() + " changed password for " + userid);
}

void cmd_useradd(const std::vector<std::string> &tokens) {
    // useradd [UserID] [Password] [Privilege] [Username]
    if (tokens.size() != 5) {
        std::cout << "Invalid\n";
        return;
    }
    if (current_privilege() < 3) {
        std::cout << "Invalid\n";
        return;
    }

    std::string userid = tokens[1];
    std::string password = tokens[2];
    std::string priv_str = tokens[3];
    std::string username = tokens[4];

    if (!is_valid_userid(userid) || !is_valid_password(password) ||
        !is_valid_privilege(priv_str) || !is_valid_username(username)) {
        std::cout << "Invalid\n";
        return;
    }

    int priv = priv_str[0] - '0';
    if (priv >= current_privilege()) {
        std::cout << "Invalid\n";
        return;
    }

    if (find_account_pos(userid) != -1) {
        std::cout << "Invalid\n";
        return;
    }

    Account acc;
    strncpy(acc.userid, userid.c_str(), 30);
    strncpy(acc.password, password.c_str(), 30);
    strncpy(acc.username, username.c_str(), 30);
    acc.privilege = priv;
    int pos = account_store.add(acc);
    userid_index.insert(userid, pos);

    log_system.add(current_userid() + " useradd " + userid + " privilege " + priv_str);
    if (current_privilege() == 3) {
        employee_log.add(current_userid(), "useradd " + userid + " privilege " + priv_str);
    }
}

void cmd_delete(const std::vector<std::string> &tokens) {
    // delete [UserID]
    if (tokens.size() != 2) {
        std::cout << "Invalid\n";
        return;
    }
    if (current_privilege() < 7) {
        std::cout << "Invalid\n";
        return;
    }

    std::string userid = tokens[1];
    if (!is_valid_userid(userid)) {
        std::cout << "Invalid\n";
        return;
    }

    int pos = find_account_pos(userid);
    if (pos == -1) {
        std::cout << "Invalid\n";
        return;
    }

    if (count_logged_in(userid) > 0) {
        std::cout << "Invalid\n";
        return;
    }

    userid_index.remove(userid, pos);

    log_system.add(current_userid() + " deleted account " + userid);
}

void cmd_show_books(const std::vector<std::string> &tokens) {
    // show (-ISBN=[ISBN] | -name="[BookName]" | -author="[Author]" | -keyword="[Keyword]")?
    if (current_privilege() < 1) {
        std::cout << "Invalid\n";
        return;
    }

    std::vector<int> positions;
    bool found = false;

    if (tokens.size() == 1) {
        // Show all books
        positions = isbn_index.get_all();
        found = true;
    } else if (tokens.size() == 2) {
        std::string param = tokens[1];
        if (param.substr(0, 6) == "-ISBN=") {
            std::string isbn = param.substr(6);
            if (!is_valid_isbn(isbn)) {
                std::cout << "Invalid\n";
                return;
            }
            int pos = find_book_pos(isbn);
            if (pos != -1) positions.push_back(pos);
            found = true;
        } else if (param.size() > 7 && param.substr(0, 7) == "-name=\"" && param.back() == '"') {
            std::string name = param.substr(7, param.size() - 8);
            if (!is_valid_bookname(name)) {
                std::cout << "Invalid\n";
                return;
            }
            positions = name_index.find(name);
            found = true;
        } else if (param.size() > 9 && param.substr(0, 9) == "-author=\"" && param.back() == '"') {
            std::string author = param.substr(9, param.size() - 10);
            if (!is_valid_author(author)) {
                std::cout << "Invalid\n";
                return;
            }
            positions = author_index.find(author);
            found = true;
        } else if (param.size() > 10 && param.substr(0, 10) == "-keyword=\"" && param.back() == '"') {
            std::string keyword = param.substr(10, param.size() - 11);
            if (!is_valid_keyword(keyword)) {
                std::cout << "Invalid\n";
                return;
            }
            // keyword must be a single keyword (no '|')
            if (keyword.find('|') != std::string::npos) {
                std::cout << "Invalid\n";
                return;
            }
            positions = keyword_index.find(keyword);
            found = true;
        } else {
            std::cout << "Invalid\n";
            return;
        }
    } else {
        std::cout << "Invalid\n";
        return;
    }

    if (!found) {
        std::cout << "Invalid\n";
        return;
    }

    // Collect books and sort by ISBN
    std::vector<std::pair<std::string, int>> books;
    for (int pos : positions) {
        Book b;
        book_store.read(pos, b);
        books.push_back({std::string(b.isbn), pos});
    }
    std::sort(books.begin(), books.end());

    // Remove duplicates (same book position)
    std::vector<std::pair<std::string, int>> unique_books;
    for (auto &p : books) {
        if (unique_books.empty() || unique_books.back().second != p.second) {
            unique_books.push_back(p);
        }
    }

    if (unique_books.empty()) {
        std::cout << "\n";
    } else {
        for (auto &p : unique_books) {
            Book b;
            book_store.read(p.second, b);
            std::cout << b.isbn << "\t" << b.name << "\t" << b.author << "\t"
                      << b.keyword << "\t" << std::fixed << std::setprecision(2)
                      << b.price << "\t" << b.stock << "\n";
        }
    }
}

void cmd_buy(const std::vector<std::string> &tokens) {
    // buy [ISBN] [Quantity]
    if (tokens.size() != 3) {
        std::cout << "Invalid\n";
        return;
    }
    if (current_privilege() < 1) {
        std::cout << "Invalid\n";
        return;
    }

    std::string isbn = tokens[1];
    std::string qty_str = tokens[2];

    if (!is_valid_isbn(isbn) || !is_positive_quantity(qty_str)) {
        std::cout << "Invalid\n";
        return;
    }

    int pos = find_book_pos(isbn);
    if (pos == -1) {
        std::cout << "Invalid\n";
        return;
    }

    long long qty = 0;
    for (char c : qty_str) qty = qty * 10 + (c - '0');

    Book b;
    book_store.read(pos, b);
    if (b.stock < qty) {
        std::cout << "Invalid\n";
        return;
    }

    double total = b.price * qty;
    b.stock -= (int)qty;
    book_store.write(pos, b);

    std::cout << std::fixed << std::setprecision(2) << total << "\n";

    finance_log.add(total, 0);
    log_system.add(current_userid() + " bought " + qty_str + " of " + isbn + " for " + std::to_string(total));
}

void cmd_select(const std::vector<std::string> &tokens) {
    // select [ISBN]
    if (tokens.size() != 2) {
        std::cout << "Invalid\n";
        return;
    }
    if (current_privilege() < 3) {
        std::cout << "Invalid\n";
        return;
    }

    std::string isbn = tokens[1];
    if (!is_valid_isbn(isbn)) {
        std::cout << "Invalid\n";
        return;
    }

    int pos = find_book_pos(isbn);
    if (pos == -1) {
        // Create new book
        Book b;
        strncpy(b.isbn, isbn.c_str(), 20);
        pos = book_store.add(b);
        isbn_index.insert(isbn, pos);
    }

    login_stack.back().selected_book_pos = pos;

    log_system.add(current_userid() + " selected book " + isbn);
    if (current_privilege() == 3) {
        employee_log.add(current_userid(), "selected book " + isbn);
    }
}

void cmd_modify(const std::vector<std::string> &tokens) {
    // modify (-ISBN=[ISBN] | -name="[BookName]" | -author="[Author]" | -keyword="[Keyword]" | -price=[Price])+
    if (tokens.size() < 2) {
        std::cout << "Invalid\n";
        return;
    }
    if (current_privilege() < 3) {
        std::cout << "Invalid\n";
        return;
    }
    if (login_stack.back().selected_book_pos == -1) {
        std::cout << "Invalid\n";
        return;
    }

    int book_pos = login_stack.back().selected_book_pos;
    Book b;
    book_store.read(book_pos, b);

    // Parse parameters
    bool has_isbn = false, has_name = false, has_author = false, has_keyword = false, has_price = false;
    std::string new_isbn, new_name, new_author, new_keyword;
    double new_price = 0;

    for (size_t i = 1; i < tokens.size(); ++i) {
        const std::string &param = tokens[i];
        if (param.substr(0, 6) == "-ISBN=") {
            if (has_isbn) { std::cout << "Invalid\n"; return; }
            has_isbn = true;
            new_isbn = param.substr(6);
            if (!is_valid_isbn(new_isbn)) { std::cout << "Invalid\n"; return; }
        } else if (param.size() > 7 && param.substr(0, 7) == "-name=\"" && param.back() == '"') {
            if (has_name) { std::cout << "Invalid\n"; return; }
            has_name = true;
            new_name = param.substr(7, param.size() - 8);
            if (!is_valid_bookname(new_name)) { std::cout << "Invalid\n"; return; }
        } else if (param.size() > 9 && param.substr(0, 9) == "-author=\"" && param.back() == '"') {
            if (has_author) { std::cout << "Invalid\n"; return; }
            has_author = true;
            new_author = param.substr(9, param.size() - 10);
            if (!is_valid_author(new_author)) { std::cout << "Invalid\n"; return; }
        } else if (param.size() > 10 && param.substr(0, 10) == "-keyword=\"" && param.back() == '"') {
            if (has_keyword) { std::cout << "Invalid\n"; return; }
            has_keyword = true;
            new_keyword = param.substr(10, param.size() - 11);
            if (!is_valid_keyword(new_keyword)) { std::cout << "Invalid\n"; return; }
        } else if (param.substr(0, 7) == "-price=") {
            if (has_price) { std::cout << "Invalid\n"; return; }
            has_price = true;
            std::string price_str = param.substr(7);
            if (!is_valid_price(price_str)) { std::cout << "Invalid\n"; return; }
            new_price = std::stod(price_str);
        } else {
            std::cout << "Invalid\n";
            return;
        }
    }

    // Check ISBN change validity
    if (has_isbn) {
        if (new_isbn == std::string(b.isbn)) {
            std::cout << "Invalid\n";
            return;
        }
        if (find_book_pos(new_isbn) != -1) {
            std::cout << "Invalid\n";
            return;
        }
    }

    // Check keyword validity
    if (has_keyword) {
        auto kws = split_keywords(new_keyword);
        for (auto &kw : kws) {
            if (kw.empty()) { std::cout << "Invalid\n"; return; }
        }
        if (has_duplicate_keywords(kws)) { std::cout << "Invalid\n"; return; }
    }

    // Apply changes
    if (has_isbn) {
        // Remove old ISBN index, add new
        isbn_index.remove(std::string(b.isbn), book_pos);
        strncpy(b.isbn, new_isbn.c_str(), 20);
        b.isbn[20] = 0;
        isbn_index.insert(new_isbn, book_pos);
    }

    if (has_name) {
        // Remove old name index if it exists
        if (strlen(b.name) > 0) {
            name_index.remove(std::string(b.name), book_pos);
        }
        strncpy(b.name, new_name.c_str(), 60);
        b.name[60] = 0;
        name_index.insert(new_name, book_pos);
    }

    if (has_author) {
        if (strlen(b.author) > 0) {
            author_index.remove(std::string(b.author), book_pos);
        }
        strncpy(b.author, new_author.c_str(), 60);
        b.author[60] = 0;
        author_index.insert(new_author, book_pos);
    }

    if (has_keyword) {
        // Remove old keywords
        if (strlen(b.keyword) > 0) {
            auto old_kws = split_keywords(std::string(b.keyword));
            for (auto &kw : old_kws) {
                if (!kw.empty()) {
                    keyword_index.remove(kw, book_pos);
                }
            }
        }
        strncpy(b.keyword, new_keyword.c_str(), 60);
        b.keyword[60] = 0;
        auto new_kws = split_keywords(new_keyword);
        for (auto &kw : new_kws) {
            keyword_index.insert(kw, book_pos);
        }
    }

    if (has_price) {
        b.price = new_price;
    }

    book_store.write(book_pos, b);

    log_system.add(current_userid() + " modified book " + std::string(b.isbn));
    if (current_privilege() == 3) {
        employee_log.add(current_userid(), "modified book " + std::string(b.isbn));
    }
}

void cmd_import(const std::vector<std::string> &tokens) {
    // import [Quantity] [TotalCost]
    if (tokens.size() != 3) {
        std::cout << "Invalid\n";
        return;
    }
    if (current_privilege() < 3) {
        std::cout << "Invalid\n";
        return;
    }
    if (login_stack.back().selected_book_pos == -1) {
        std::cout << "Invalid\n";
        return;
    }

    std::string qty_str = tokens[1];
    std::string cost_str = tokens[2];

    if (!is_positive_quantity(qty_str) || !is_positive_price(cost_str)) {
        std::cout << "Invalid\n";
        return;
    }

    long long qty = 0;
    for (char c : qty_str) qty = qty * 10 + (c - '0');
    double cost = std::stod(cost_str);

    int book_pos = login_stack.back().selected_book_pos;
    Book b;
    book_store.read(book_pos, b);
    b.stock += (int)qty;
    book_store.write(book_pos, b);

    finance_log.add(0, cost);

    log_system.add(current_userid() + " imported " + qty_str + " of " + std::string(b.isbn) + " cost " + cost_str);
    if (current_privilege() == 3) {
        employee_log.add(current_userid(), "imported " + qty_str + " of " + std::string(b.isbn) + " cost " + cost_str);
    }
}

void cmd_show_finance(const std::vector<std::string> &tokens) {
    // show finance ([Count])?
    if (current_privilege() < 7) {
        std::cout << "Invalid\n";
        return;
    }

    if (tokens.size() == 2) {
        // show finance - all transactions
        double income = 0, expense = 0;
        int total = finance_log.get_count();
        if (total > 0) {
            finance_log.query(total, income, expense);
        }
        std::cout << "+ " << std::fixed << std::setprecision(2) << income
                  << " - " << std::fixed << std::setprecision(2) << expense << "\n";
    } else if (tokens.size() == 3) {
        std::string count_str = tokens[2];
        if (!is_valid_count(count_str)) {
            std::cout << "Invalid\n";
            return;
        }
        long long count = 0;
        for (char c : count_str) count = count * 10 + (c - '0');

        if (count == 0) {
            std::cout << "\n";
            return;
        }

        int total = finance_log.get_count();
        if (count > total) {
            std::cout << "Invalid\n";
            return;
        }

        double income = 0, expense = 0;
        finance_log.query((int)count, income, expense);
        std::cout << "+ " << std::fixed << std::setprecision(2) << income
                  << " - " << std::fixed << std::setprecision(2) << expense << "\n";
    } else {
        std::cout << "Invalid\n";
    }
}

void cmd_log(const std::vector<std::string> &tokens) {
    if (tokens.size() != 1) {
        std::cout << "Invalid\n";
        return;
    }
    if (current_privilege() < 7) {
        std::cout << "Invalid\n";
        return;
    }
    log_system.print_all();
}

void cmd_report_finance(const std::vector<std::string> &tokens) {
    if (tokens.size() != 2) {
        std::cout << "Invalid\n";
        return;
    }
    if (current_privilege() < 7) {
        std::cout << "Invalid\n";
        return;
    }
    // Print finance report
    std::cout << "Finance Report\n";
    double income = 0, expense = 0;
    int total = finance_log.get_count();
    if (total > 0) {
        finance_log.query(total, income, expense);
    }
    std::cout << "Total Income: " << std::fixed << std::setprecision(2) << income << "\n";
    std::cout << "Total Expense: " << std::fixed << std::setprecision(2) << expense << "\n";
    std::cout << "Net: " << std::fixed << std::setprecision(2) << income - expense << "\n";
}

void cmd_report_employee(const std::vector<std::string> &tokens) {
    if (tokens.size() != 2) {
        std::cout << "Invalid\n";
        return;
    }
    if (current_privilege() < 7) {
        std::cout << "Invalid\n";
        return;
    }
    employee_log.print_all();
}

// ============================================================
// Main
// ============================================================

int main() {
    // Initialize all storage
    account_store.init("accounts.dat");
    book_store.init("books.dat");
    userid_index.init("userid_idx.dat");
    isbn_index.init("isbn_idx.dat");
    name_index.init("name_idx.dat");
    author_index.init("author_idx.dat");
    keyword_index.init("keyword_idx.dat");
    finance_log.init("finance.dat");
    log_system.init("log.dat");
    employee_log.init("employee.dat");

    // Check if root account exists, create if not
    if (find_account_pos("root") == -1) {
        Account root;
        strncpy(root.userid, "root", 30);
        strncpy(root.password, "sjtu", 30);
        strncpy(root.username, "root", 30);
        root.privilege = 7;
        int pos = account_store.add(root);
        userid_index.insert("root", pos);
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        auto tokens = tokenize(line);
        if (tokens.empty()) continue;

        std::string cmd = tokens[0];

        if (cmd == "quit" || cmd == "exit") {
            if (tokens.size() != 1) {
                std::cout << "Invalid\n";
                continue;
            }
            break;
        } else if (cmd == "su") {
            cmd_su(tokens);
        } else if (cmd == "logout") {
            cmd_logout(tokens);
        } else if (cmd == "register") {
            cmd_register(tokens);
        } else if (cmd == "passwd") {
            cmd_passwd(tokens);
        } else if (cmd == "useradd") {
            cmd_useradd(tokens);
        } else if (cmd == "delete") {
            cmd_delete(tokens);
        } else if (cmd == "show") {
            if (tokens.size() >= 2 && tokens[1] == "finance") {
                cmd_show_finance(tokens);
            } else {
                cmd_show_books(tokens);
            }
        } else if (cmd == "buy") {
            cmd_buy(tokens);
        } else if (cmd == "select") {
            cmd_select(tokens);
        } else if (cmd == "modify") {
            cmd_modify(tokens);
        } else if (cmd == "import") {
            cmd_import(tokens);
        } else if (cmd == "log") {
            cmd_log(tokens);
        } else if (cmd == "report") {
            if (tokens.size() >= 2 && tokens[1] == "finance") {
                cmd_report_finance(tokens);
            } else if (tokens.size() >= 2 && tokens[1] == "employee") {
                cmd_report_employee(tokens);
            } else {
                std::cout << "Invalid\n";
            }
        } else {
            std::cout << "Invalid\n";
        }
    }

    return 0;
}
