// Stategine - a face: which picture someone's face wears, a state of its own.
//
// A face is pictures, worn in turn by the part of a skin that is the face (a
// skin `skin_face`, when its model has one; else the whole skin): some idle
// (a face at rest - one, or many that change, as a mask of photographs
// does), and some talking, each with the mouth more open than the last. The
// state chooses which; what wears it is told by a kept functor. It knows no
// room and no one: what it is told (`open`, how open the mouth is as words
// are said - 0 shut .. 1 wide) comes by a functor from whatever speaks, and
// what it shows (`shown`) goes by a functor to whoever wears it.
//
// Objects:  face   idle (how many idle pictures), talk (how many talking),
//                  open, shown (the picture: the idle ones, then the
//                  talking), speaks (open past which it is talking), widths
//                  (how open, for each talking picture: "0.4 0.75"; evenly
//                  if unsaid), cell, change (idle: its time cut in cells of
//                  `cell` seconds, each a toss whether the face changes there)
// Arrows:   face --<id>.tick--> face  {dt, time}, its own line: the picture
//                                     for now - talking, its first idle one,
//                                     or the talking one as open as said;
//                                     still, an idle one at random, by its
//                                     time alone (the same time, the same face)
//
// Its time is its line on a Temporal (`sg::drive`, trigger `<id>.tick`).
// Later a face may say more than a picture (weights of a model's shapes) -
// the state, its arrow and its functors stay as they are.
#pragma once

#include <string>

#include "sg/core/StateGraph.hpp"

namespace sg {

class Spatial3D;

class Face : public State {
public:
    // `idle` idle pictures, `talk` talking ones.
    Face(Key id, int idle, int talk);

    Key kind() const override { return Key{"face"}; }
    static Key face_id() { return Key{"face"}; }
    Key tick_event() const { return Key{id().str() + ".tick"}; }

    // The picture shown at `time` with the mouth `open` (pure in its params).
    int at(double time, double open) const;
};

// --- the pictures a face is ------------------------------------------------------------
// They are pictures of the room a being is shown in (`Spatial3D::picture`),
// worn by its skins: the skins on `anchor` (where it is shown: `show_skins`).
// The skin worn as a face says which (`faces`: their names, the idle ones
// first; `faces_idle`) - a face is made only on a skin that has a picture,
// and only once.

// Read from `dir` (by `Being::files()`): idle0.png, idle1.png ... (as many as
// there are), then talk1.png, talk2.png ... - each as large as the skin's own
// picture. How many idle and talking there are (0, 0 if none).
struct FaceCount {
    int idle = 0, talk = 0;
};
FaceCount faces_from(Spatial3D& host, Key anchor, const std::string& dir);

// Made from the skin's own picture and a map of where its mouth is on it
// (red: how open the mouth must be before a texel is inside it, 255 never;
// green over half: the upper lip's side, where the teeth show): `levels`
// talking pictures, more open each - the dark of a mouth, teeth under the
// upper lip, the lips just outside it a little paler. The skin's own picture
// is its one idle face. Read by `Being::files()`.
FaceCount mouths_from(Spatial3D& host, Key anchor, const std::string& map, int levels = 3);

// The faces worn on `anchor`'s skins, and the `k`th of them worn (whoever
// draws may wear each once, to have it on the card before it is needed).
FaceCount face_count(const Spatial3D& host, Key anchor);
void wear_face(Spatial3D& host, Key anchor, int k);

// --- the functors round a face ---------------------------------------------------------
// The picture `face` shows, worn by `anchor`'s face in `host`: kept, and
// carried once now. False if there is no face there to wear it.
bool wears_face(StateGraph& g, const Face& face, Spatial3D& host, Key anchor);
// How open a mouth is - `param` of `element` of `state` (what says the
// words) - to `face`: kept.
Functor& voices(StateGraph& g, const Face& face, Key state, Key element, Key param = Key{"mouth"});

}  // namespace sg
