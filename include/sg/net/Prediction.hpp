// Disposable numeric forecasts. Only finalized values become world data.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>
namespace sg::net {
using PredictedValues = std::vector<double>;
using DiscreteInputs = std::vector<std::int64_t>;
struct PredictionCorrection { bool applied = false; std::uint64_t replayed = 0; double maximum_error = 0; };
class Prediction {
public:
    using Step = std::function<PredictedValues(const PredictedValues&, const DiscreteInputs&)>;
    Prediction(Step step, std::uint64_t maximum_lead = 12, std::size_t history = 128,
               std::vector<std::uint8_t> continuous = {}, double correction_seconds = 0.1);
    void reset(std::uint64_t tick, PredictedValues values);
    bool advance(DiscreteInputs inputs);
    bool amend(std::uint64_t tick, DiscreteInputs inputs, double time);
    PredictionCorrection reconcile(std::uint64_t tick, PredictedValues values, double time);
    const PredictedValues& predicted() const;
    PredictedValues display(double temporal_time) const;
    std::optional<PredictedValues> sample(std::uint64_t tick) const;
    std::optional<PredictedValues> finalized_sample(std::uint64_t tick) const;
    std::uint64_t finalized_tick() const;
    std::uint64_t predicted_tick() const;
private:
    struct Frame { PredictedValues values; DiscreteInputs inputs; };
    Step step_;
    std::uint64_t maximum_lead_, finalized_ = 0, predicted_ = 0;
    std::size_t history_;
    std::vector<std::uint8_t> continuous_;
    std::map<std::uint64_t,Frame> frames_;
    PredictedValues offset_;
    double corrected_at_ = 0, correction_seconds_;
    void validate(const PredictedValues& values) const;
    void replay(std::uint64_t from);
    void correction(const PredictedValues& before, double time);
};
}
