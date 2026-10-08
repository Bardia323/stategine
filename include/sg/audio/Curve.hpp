// Stategine - how a sound is heard with distance, as data.
//
// A curve class (small, voice, machine, ambient ...) is an element of kind
// `curve`, its rows in columns of numbers, one text each, in order of
// distance:
//
//   d       metres
//   db      how loud at that distance, in dB
//   send    how much goes to the reverb of its place
//   cut     the top the air leaves, a low-pass's cut-off in Hz
//   spread  how little it comes from one side: 1 all round, 0 a point
//
// Between two rows it is a straight line; before the first and past the
// last, the end rows. One look up is a binary search and a line, the cost of
// the formula it replaces. A class no state declares is heard by the
// standard one, which is that formula written as rows.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "sg/core/StateGraph.hpp"

namespace sg::audio {

// What a curve says at one distance.
struct Heard {
    double gain = 1.0;  // as a gain, 10^(dB/20)
    double send = 0.0;
    double cut = 20000.0;
    double spread = 0.0;
};

struct Curve {
    struct Row {
        double d = 0, db = 0, send = 0, cut = 20000, spread = 0;
    };
    std::vector<Row> rows;  // in order of distance

    Heard at(double d) const;
};

// A curve from its element's columns. False, with `why`, if a column is
// missing, the columns are of different lengths, a number is not one or not
// finite, or the distances go backwards.
bool curve_of(const Element& e, Curve& out, std::string* why = nullptr);

// Distance falls off as 1 / d^0.95 past 0.6 m, the air dulls it by 18 kHz /
// (1 + d / 25), a far one sends more to the reverb, a near one comes from
// both sides: what sound.hpp heard everything by, before it was data.
const Curve& standard_curve();

// Every curve class a graph declares, by name, read again when the graph or
// one of them changes.
class Curves {
public:
    void read(const StateGraph& g);
    // The class `name`, or the standard one.
    const Curve& of(Key name) const;
    bool has(Key name) const { return curves_.count(name) != 0; }

private:
    struct Where {
        Key state, element;
        uint64_t stamp = 0;
    };
    std::vector<Where> where_;
    std::unordered_map<Key, Curve> curves_;
    uint64_t revision_ = ~uint64_t{0};
};

// What is wrong with the curves a graph declares, one line each.
std::vector<std::string> curve_defects(const StateGraph& g);

}  // namespace sg::audio
