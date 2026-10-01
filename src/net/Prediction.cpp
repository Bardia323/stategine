#include "sg/net/Prediction.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace sg::net {
Prediction::Prediction(Step step, std::uint64_t lead, std::size_t history, std::vector<std::uint8_t> continuous, double seconds)
    : step_(std::move(step)),maximum_lead_(lead),history_(history),continuous_(std::move(continuous)),correction_seconds_(seconds) {
    if (!step_ || !lead || history <= lead || !std::isfinite(seconds) || seconds < 0) throw std::invalid_argument("net: invalid prediction limits");
    for (auto flag : continuous_) if (flag > 1) throw std::invalid_argument("net: invalid prediction continuity mask");
}
void Prediction::validate(const PredictedValues& values) const {
    if (values.empty() || (!frames_.empty() && values.size() != frames_.begin()->second.values.size()) || (!continuous_.empty() && continuous_.size() != values.size())) throw std::invalid_argument("net: invalid predicted coordinate count");
    for (auto x : values) if (!std::isfinite(x)) throw std::invalid_argument("net: nonfinite prediction");
}
void Prediction::reset(std::uint64_t tick, PredictedValues values) { frames_.clear(); validate(values); finalized_ = predicted_ = tick; offset_.assign(values.size(),0); frames_.emplace(tick,Frame{std::move(values),{}}); }
bool Prediction::advance(DiscreteInputs inputs) {
    if (frames_.empty()) throw std::logic_error("net: prediction needs a finalized start");
    if (predicted_-finalized_ >= maximum_lead_ || predicted_ == UINT64_MAX) return false;
    auto values = step_(predicted(),inputs); validate(values); ++predicted_; frames_[predicted_] = {std::move(values),std::move(inputs)};
    while (frames_.size() > history_) frames_.erase(frames_.begin()); return true;
}
void Prediction::replay(std::uint64_t from) { if (from == UINT64_MAX) return; for (auto tick = from+1; tick <= predicted_; ++tick) { auto& frame = frames_.at(tick); auto values = step_(frames_.at(tick-1).values,frame.inputs); validate(values); frame.values = std::move(values); if (tick == UINT64_MAX) break; } }
void Prediction::correction(const PredictedValues& before, double time) {
    if (!std::isfinite(time)) throw std::invalid_argument("net: correction needs declared Temporal time");
    const auto& now = predicted(); offset_.resize(now.size()); for (std::size_t i = 0; i < now.size(); ++i) offset_[i] = continuous_.empty() || continuous_[i] ? before[i]-now[i] : 0; corrected_at_ = time;
}
bool Prediction::amend(std::uint64_t tick, DiscreteInputs inputs, double time) { if (tick <= finalized_ || tick > predicted_ || !frames_.count(tick) || frames_.at(tick).inputs == inputs) return false; const auto before = display(time); frames_.at(tick).inputs = std::move(inputs); replay(tick-1); correction(before,time); return true; }
PredictionCorrection Prediction::reconcile(std::uint64_t tick, PredictedValues values, double time) {
    if (tick <= finalized_) return {};
    validate(values); const auto before = display(time); PredictionCorrection out; out.applied = true; const auto old = frames_.find(tick);
    const auto& comparison = old == frames_.end() ? before : old->second.values;
    for (std::size_t i = 0; i < values.size(); ++i) out.maximum_error = std::max(out.maximum_error,std::fabs(comparison[i]-values[i]));
    if (tick > predicted_) { predicted_ = tick; frames_.clear(); }
    // A checkpoint may skip speculative intermediate frames. Those frames
    // were never finalized, so they cannot enter verified historical queries.
    for (auto it = frames_.upper_bound(finalized_); it != frames_.end() && it->first < tick;) it = frames_.erase(it);
    frames_[tick].values = std::move(values); finalized_ = tick; out.replayed = predicted_-tick; replay(tick); correction(before,time);
    while (frames_.size() > history_) frames_.erase(frames_.begin()); return out;
}
const PredictedValues& Prediction::predicted() const { if (frames_.empty()) throw std::logic_error("net: empty prediction"); return frames_.at(predicted_).values; }
PredictedValues Prediction::display(double time) const { if (!std::isfinite(time)) throw std::invalid_argument("net: display needs declared Temporal time"); auto values = predicted(); const double alpha = correction_seconds_ == 0 ? 0 : std::clamp(1-(time-corrected_at_)/correction_seconds_,0.0,1.0); for (std::size_t i = 0; i < values.size(); ++i) values[i] += alpha*offset_[i]; return values; }
std::optional<PredictedValues> Prediction::sample(std::uint64_t tick) const { const auto found = frames_.find(tick); return found == frames_.end() ? std::nullopt : std::optional<PredictedValues>{found->second.values}; }
std::optional<PredictedValues> Prediction::finalized_sample(std::uint64_t tick) const { return tick <= finalized_ ? sample(tick) : std::nullopt; }
std::uint64_t Prediction::finalized_tick() const { return finalized_; }
std::uint64_t Prediction::predicted_tick() const { return predicted_; }
}
