/* Host test for the Pocket build's compat std headers.  Build with
 * `make -C pocket test`: it compiles against compat/cxx with -nostdinc++,
 * the way the device build sees them. */
#include <stdio.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond) \
    do \
    { \
        if (!(cond)) \
        { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++; \
        } \
    } while (0)

struct Counted
{
    static int live;
    int value;
    Counted() : value(0) { live++; }
    Counted(int v) : value(v) { live++; }
    Counted(const Counted &o) : value(o.value) { live++; }
    ~Counted() { live--; }
};
int Counted::live = 0;

static void test_string(void)
{
    std::string empty;
    CHECK(empty.empty() && empty.size() == 0 && empty.c_str()[0] == '\0');
    empty.clear();
    CHECK(empty.c_str()[0] == '\0');

    std::string s("hello");
    s += ' ';
    s += "world";
    CHECK(s == "hello world" && s.length() == 11);
    CHECK(s.find("world") == 6 && s.find('o') == 4 && s.find('o', 5) == 7);
    CHECK(s.find("xyz") == std::string::npos && s.find('z') == std::string::npos);
    CHECK(s.find("") == 0 && s.find("", 11) == 11);
    CHECK(s.rfind('o') == 7 && s.find_last_of("lo") == 9 && s.find_first_of("wd") == 6);
    CHECK(s.substr(6) == "world" && s.substr(0, 5) == "hello" && s.substr(11).empty());
    CHECK(s.compare(0, 5, "hello") == 0 && s.compare(0, 5, "help") < 0);
    CHECK(*s.rbegin() == 'd' && s.back() == 'd' && s[0] == 'h' && s.at(1) == 'e');

    s.replace(0, 5, "goodbye");
    CHECK(s == "goodbye world");
    s.replace(0, 7, "hi");
    CHECK(s == "hi world");
    s.erase(2, 1);
    CHECK(s == "hiworld");
    s.erase(s.end() - 1);
    CHECK(s == "hiworl");
    s.erase(4);
    CHECK(s == "hiwo");
    s.insert(2, "--");
    CHECK(s == "hi--wo");
    s.append(s.c_str() + 4, 2); /* overlapping self-append */
    CHECK(s == "hi--wowo");
    s.assign(s.c_str() + 2, 3); /* overlapping self-assign */
    CHECK(s == "--w");
    s.resize(5, 'x');
    CHECK(s == "--wxx");
    s.resize(1);
    CHECK(s == "-" && s.c_str()[1] == '\0');
    s.push_back('a');
    s.pop_back();
    CHECK(s == "-");

    std::string a = std::string("ab") + "cd" + 'e';
    CHECK(a == "abcde" && "abcde" == a && a != "abcd");
    CHECK(std::string("abc") < std::string("abd") && std::string("ab") < std::string("abc"));

    std::string big;
    for (int i = 0; i < 1000; i++)
    {
        big += (char) ('a' + i % 26);
    }
    CHECK(big.size() == 1000 && big[999] == 'a' + 999 % 26);
    std::string copy = big;
    copy[0] = 'Z';
    CHECK(big[0] == 'a' && copy[0] == 'Z');

    std::string unicode("caf\xc3\xa9");
    CHECK(unicode.size() == 5);
    CHECK(std::string(3, 'x') == "xxx");
}

static void test_vector(void)
{
    {
        std::vector<Counted> v;
        for (int i = 0; i < 100; i++)
        {
            v.push_back(Counted(i));
        }
        CHECK(v.size() == 100 && Counted::live == 100);
        v.push_back(v[0]); /* self-reference across a reallocation */
        CHECK(v.back().value == 0);
        v.erase(v.begin() + 10);
        CHECK(v[10].value == 11 && v.size() == 100 && Counted::live == 100);
        v.insert(v.begin() + 10, Counted(10));
        CHECK(v[10].value == 10 && v[11].value == 11 && v.size() == 101);
        v.resize(5);
        CHECK(v.size() == 5 && Counted::live == 5);
        std::vector<Counted> w(v);
        CHECK(w.size() == 5 && Counted::live == 10);
        w.clear();
        CHECK(Counted::live == 5);
    }
    CHECK(Counted::live == 0);

    static const int arr[] = {3, 1, 4, 1, 5};
    std::vector<int> v;
    v.assign(arr, arr + 5);
    CHECK(v.size() == 5 && v[2] == 4);
    v.insert(v.end(), arr, arr + 2);
    CHECK(v.size() == 7 && v[5] == 3 && v[6] == 1);
    v.insert(v.begin(), arr + 3, arr + 5);
    CHECK(v[0] == 1 && v[1] == 5 && v[2] == 3);
    int sum = 0;
    for (std::vector<int>::iterator it = v.begin(); it != v.end(); ++it)
    {
        sum += *it;
    }
    CHECK(sum == 1 + 5 + 3 + 1 + 4 + 1 + 5 + 3 + 1);
    std::vector<int> filled(4, 7);
    CHECK(filled.size() == 4 && filled[3] == 7);

    std::vector<std::string> strings;
    strings.push_back("a");
    strings.push_back("b");
    strings.insert(strings.begin(), "");
    CHECK(strings.size() == 3 && strings[0].empty() && strings[2] == "b");
    static const char *lines[] = {"x", "y"};
    strings.insert(strings.end(), lines, lines + 2); /* other iterator type */
    CHECK(strings.size() == 5 && strings[3] == "x" && strings[4] == "y");
}

static void test_map(void)
{
    std::map<std::string, int> m;
    m["b"] = 2;
    m["a"] = 1;
    m["c"] = 3;
    m["b"] += 10;
    CHECK(m.size() == 3 && m["b"] == 12 && m.count("a") == 1 && m.count("z") == 0);
    std::string keys;
    for (std::map<std::string, int>::iterator it = m.begin(); it != m.end(); ++it)
    {
        keys += it->first;
    }
    CHECK(keys == "abc");
    CHECK(m.find("c") != m.end() && m.find("c")->second == 3 && m.find("d") == m.end());
    CHECK(m.erase("a") == 1 && m.erase("a") == 0 && m.size() == 2);

    std::map<int, bool> keymap;
    keymap[50] = true;
    keymap[-3] = false;
    keymap.erase(50);
    CHECK(keymap.size() == 1 && keymap.begin()->first == -3);
}

static void test_sstream(void)
{
    std::ostringstream os;
    os << -42;
    CHECK(os.str() == "-42");
}

int main(void)
{
    test_string();
    test_vector();
    test_map();
    test_sstream();
    if (failures != 0)
    {
        printf("%d compat check(s) failed\n", failures);
        return 1;
    }
    printf("compat checks passed\n");
    return 0;
}
