// Stategine - what a step does, declared.
//
// An arrow or a transport is a function, and the engine runs it. Most say
// nothing of what they do: they may do anything, and the only way to know is
// to run them (Laws.hpp). Some can say it: every parameter they set is a sum
// of parameters they read, each times a number - and perhaps times one of
// the event's arguments (a step's `dt`) - plus a number:
//
//     x  <-  x + vx * dt            an integrator
//     z  <-  y                      a map whose z is the world's y
//     every parameter, as it is     a copy
//
// That is an Affine: rows, run one after another, each setting one parameter
// of the element written, from those of the element read (or, `of_target`,
// the one written). A row runs only if every parameter it reads is there; a
// row that reads one parameter as it is (k = 1, nothing added, no argument)
// is a copy, of whatever the value is - a word, a flag - and any other is
// arithmetic, on numbers.
//
// Declared, the description is the step: `run` below is how the engine runs
// it (the arrow's handler, the transport's function are made from it), and
// it is what a compiler reads (sg/algebra) to check a law without running
// anything. It says nothing a run would not do; nothing here is a second
// truth. Data only - no dependency beyond the core.
#pragma once

#include <memory>
#include <vector>

#include "sg/core/Core.hpp"

namespace sg {

struct Affine {
    struct Term {
        Key param;
        double k = 1.0;
        Key arg;                  // times this event argument, when named (0 if the event has none)
        bool of_target = false;   // read from the element written, not the one read
    };
    struct Row {
        Key param;                // what it sets, on the element written
        std::vector<Term> terms;
        double bias = 0.0;
    };
    bool copy_all = false;        // first, every parameter of the element read, as it is
    std::vector<Row> rows;        // then these, one after another

    static bool is_copy(const Row& r) {
        return r.terms.size() == 1 && r.terms[0].k == 1.0 && r.terms[0].arg.empty() && r.bias == 0.0;
    }

    Affine& copy(Key to, Key from) {
        rows.push_back(Row{to, {Term{from, 1.0, Key{}, false}}, 0.0});
        return *this;
    }
    Affine& set(Key to, std::vector<Term> terms, double bias = 0.0) {
        rows.push_back(Row{to, std::move(terms), bias});
        return *this;
    }
};

// Running it: `src` read, `dst` written (the same element, for an arrow on
// one), `args` the event's.
inline void run(const Affine& a, const Element& src, Element& dst, const Params* args = nullptr) {
    if (a.copy_all && &src != &dst)
        for (const auto& kv : src.params) dst.params.set(kv.first, kv.second);
    for (const Affine::Row& r : a.rows) {
        bool there = true;
        for (const Affine::Term& t : r.terms)
            if (!(t.of_target ? dst : src).params.has(t.param)) there = false;
        if (!there) continue;
        if (Affine::is_copy(r)) {
            const Affine::Term& t = r.terms[0];
            dst.params.set(r.param, (t.of_target ? dst : src).params.get(t.param));
            continue;
        }
        double v = r.bias;
        for (const Affine::Term& t : r.terms) {
            double k = t.k;
            if (!t.arg.empty()) k *= args ? args->num(t.arg) : 0.0;
            v += k * (t.of_target ? dst : src).params.num(t.param);
        }
        dst.params.set(r.param, v);
    }
}

// A transport, declared: stages run one after another, each from what the
// last left - through an element made fresh between them (its id and kind,
// nothing else), as a composite functor's transport is.
using Stages = std::vector<Affine>;

// A declared step of an arrow: from which element, to which (itself, when
// empty), doing what.
struct DeclaredStep {
    Key from;
    Key to;
    Affine does;
};
using DeclaredSteps = std::vector<DeclaredStep>;

}  // namespace sg
