#include "sg/net/Protocol.hpp"
#include "sg/net/Prediction.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
namespace {
using namespace sg::net;
int failures = 0;
void check(bool good, const char* why) { std::cout << (good ? "ok " : "FAIL ") << why << '\n'; failures += !good; }
template<class F> bool refused(F&& fn) { try { fn(); return false; } catch (const std::exception&) { return true; } }
Seed seed(int i) { Seed out{}; out[0] = static_cast<std::uint8_t>(i); return out; }
struct Fixture {
    std::vector<std::unique_ptr<SigningKey>> keys;
    Committee committee{"region",0,{},3,1};
    Layout l{{0,1,2},{0,1},{0,2},{0,1},{-1,1},{0,1,2},{0,0},{0,1}};
    SolverSpec spec;
    Fixture(int first = 1) { for (int i = 0; i < 4; ++i) { keys.push_back(std::make_unique<SigningKey>(seed(first+i))); committee.members.push_back({"p"+std::to_string(first+i),keys.back()->public_key()}); } }
    LinearSystem system() const { LinearSystem s{l}; s.observations = {2,10}; s.confidence = {0,0}; s.fixed = {0,0}; s.pins = {0,0}; s.overlap_weights = {1}; s.iterations = spec.iterations; return s; }
    EpochContext context(Digest previous = {},std::uint64_t tick = 0) const { const auto s = system(); return {"world","region",tick,committee.generation,topology_hash(l),constraint_hash(s,{}),spec.id(),committee.id(),previous}; }
    std::vector<Observation> inputs(const EpochContext& c) const {
        std::vector<Observation> out;
        for (int i = 0; i < 2; ++i) { Observation o{committee.members[i].peer,c.id(),c.tick,0,"object"+std::to_string(i),"value",false,{},Bytes{static_cast<std::uint8_t>(i ? 10 : 2)},{}}; o.sign(*keys[i]); out.push_back(o); } return out;
    }
    Problem problem() const { const auto c = context(); return Problem::make(c,system(),spec,inputs(c)); }
};
class Nearby final : public Backend {
public:
    bool available() const override { return true; }
    std::vector<double> solve(const LinearSystem& s) const override { auto x = CpuBackend{}.solve(s); for (std::size_t i = 0; i < x.size(); ++i) if (!s.fixed[i]) x[i] += 1e-10; return x; }
    Residual residual(const LinearSystem& s, const std::vector<double>& x) const override { return measure(s,x); }
};
}
int main() {
    Fixture f; const auto p = f.problem(); Verify verify; CpuBackend cpu;
    const auto proposal = verify.propose(p,f.committee,f.committee.members[0].peer,*f.keys[0],cpu);
    const auto good = verify.proposal(p,f.committee,proposal);
    check(good && good->values() == std::vector<double>({6,6}),"canonical solve preserves the zero-confidence mean");
    check(good && good->checkpoint_hash() == hash(good->checkpoint()),"memoized deterministic checkpoint hash retains the exact signed bytes");
    Verify cached; unsigned checkpoints = 0;
    Verify checkpoint_cache({},[&](const Problem&,const std::vector<double>& values){ ++checkpoints; return canonical_result(values,p.solver.quantum); });
    const auto cached_proposal = cached.propose(p,f.committee,f.committee.members[0].peer,*f.keys[0],cpu);
    const auto cached_result = cached.proposal(p,f.committee,cached_proposal);
    Agreement cached_agreement(f.committee);
    for (int i = 0; i < 3; ++i) {
        const auto another = cached.propose(p,f.committee,f.committee.members[i].peer,*f.keys[i],cpu);
        const auto verified = cached.proposal(p,f.committee,another);
        cached_agreement.attest(*verified,f.committee.members[i].peer,*f.keys[i]);
        checkpoint_cache.propose(p,f.committee,f.committee.members[i].peer,*f.keys[i],cpu);
    }
    const auto cached_final = cached_agreement.finalize(*cached_result,cached_proposal);
    check(cached_final && cached_agreement.accept(p,cached,*cached_final) && cached.diagnostics().reference_solves == 1 &&
          cached.diagnostics().authenticated_input_sets == 1 && checkpoints == 1,"self/remote verification, checkpoints and finalization reuse one locally authenticated reference");
    auto changed_problem = p; changed_problem.observations[0] = 9;
    check(!cached.proposal(changed_problem,f.committee,cached_proposal) && cached.diagnostics().reference_solves == 1,"a warm cache never bypasses validation of mutable Problem buffers");
    changed_problem = p; changed_problem.epoch.ordered_inputs[0].signature[0] ^= 1;
    check(!cached.proposal(changed_problem,f.committee,cached_proposal),"unchanged input digest never bypasses changed observation signatures");
    auto forged_certificate = cached_proposal; forged_certificate.certificate.signature[0] ^= 1;
    check(!cached.proposal(p,f.committee,forged_certificate),"a warm reference still authenticates every new certificate");
    Verify fresh_verifier;
    check(!fresh_verifier.proposal(p,f.committee,forged_certificate) && fresh_verifier.diagnostics().reference_solves == 0,"forged certificates are rejected before an expensive cold reference solve");
    Verify other_peer = cached; other_peer.proposal(p,f.committee,cached_proposal);
    check(other_peer.diagnostics().reference_solves == 1 && cached.diagnostics().reference_solves == 1,"copying a verifier starts an independent peer reference cache");
    auto ordered = p.epoch.ordered_inputs; std::reverse(ordered.begin(),ordered.end());
    check(Problem::make(p.epoch.context,p.system(),p.solver,ordered).epoch.id() == p.epoch.id(),"canonical input order is independent of arrival order");
    const auto close = verify.propose(p,f.committee,f.committee.members[1].peer,*f.keys[1],Nearby{});
    check(close.certificate.result == proposal.certificate.result && verify.proposal(p,f.committee,close).has_value(),"heterogeneous floating computations share independently recomputed acceptance cells");
    CudaBackend cuda;
    if (cuda.available()) { const auto gpu = verify.propose(p,f.committee,f.committee.members[2].peer,*f.keys[2],cuda); check(gpu.certificate.result == proposal.certificate.result && verify.proposal(p,f.committee,gpu).has_value(),"real CUDA and CPU verify under identical tolerances"); }
    else std::cout << "SKIP CUDA agreement: unavailable\n";
    Integrity integrity(f.committee); const auto first = p.epoch.ordered_inputs[0]; auto forged = first; forged.payload[0] ^= 1;
    check(integrity.receive(forged,p.epoch.context.id(),0) == Delivery::Invalid,"forgery is rejected before the world boundary");
    check(integrity.receive(first,p.epoch.context.id(),0) == Delivery::Accepted && integrity.receive(first,p.epoch.context.id(),0) == Delivery::Duplicate,"duplicate signed input is harmless");
    auto conflict = first; conflict.payload = {7}; conflict.sign(*f.keys[0]);
    check(integrity.receive(conflict,p.epoch.context.id(),0) == Delivery::Equivocation && integrity.evidence().size() == 1 && integrity.evidence()[0].first.authentic(f.committee) && integrity.evidence()[0].second.authentic(f.committee),"equivocation retains both signed statements");
    auto bad = proposal; bad.values = {0,0}; bad.certificate.result = hash(canonical_result(bad.values,p.solver.quantum)); bad.certificate.residual = measure(p.system(),bad.values); bad.certificate.signature = f.keys[0]->sign(bad.certificate.statement());
    check(!verify.proposal(p,f.committee,bad),"compatible wrong-kernel result fails independent recomputation");
    bad = proposal; bad.values = {100,-100}; bad.certificate.signature = f.keys[0]->sign(bad.certificate.statement()); check(!verify.proposal(p,f.committee,bad),"valid signer cannot approve an invalid result");
    auto alternate_inputs = p.epoch.ordered_inputs; alternate_inputs[0].payload = {8}; alternate_inputs[0].sign(*f.keys[0]);
    const auto alternate_problem = Problem::make(p.epoch.context,p.system(),p.solver,alternate_inputs);
    const auto alternate_proposal = verify.propose(alternate_problem,f.committee,f.committee.members[0].peer,*f.keys[0],cpu);
    const auto alternate_result = verify.proposal(alternate_problem,f.committee,alternate_proposal);
    check(alternate_result && alternate_problem.epoch.id() != p.epoch.id() && alternate_result->result() == good->result() && !verify.proposal(p,f.committee,alternate_proposal),"same result with different inputs cannot attest to the same epoch");
    Agreement a(f.committee);
    const auto speculative_context = f.context(hash(Bytes{9}),1);
    const auto speculative_problem = Problem::make(speculative_context,f.system(),f.spec,f.inputs(speculative_context));
    const auto speculative_proposal = verify.propose(speculative_problem,f.committee,f.committee.members[0].peer,*f.keys[0],cpu);
    const auto speculative = verify.proposal(speculative_problem,f.committee,speculative_proposal);
    check(speculative && refused([&]{a.attest(*speculative,f.committee.members[0].peer,*f.keys[0]);}),"speculative computation cannot attest a successor before its finalized predecessor is known");
    for (int i = 0; i < 3; ++i) a.attest(*good,f.committee.members[i].peer,*f.keys[i]);
    check(refused([&]{ a.attest(*alternate_result,f.committee.members[0].peer,*f.keys[0]); }),"executor refuses a second decision at the same world step");
    Agreement restarted(f.committee); restarted.restore(a.snapshot());
    check(refused([&]{ restarted.attest(*alternate_result,f.committee.members[0].peer,*f.keys[0]); }) && restarted.attest(*good,f.committee.members[0].peer,*f.keys[0]).decision == good->decision(),"durable vote locks survive restart and allow identical retransmission");
    Agreement failing(f.committee,{},0,[](const Bytes&){ throw std::runtime_error("disk unavailable"); });
    check(refused([&]{ failing.attest(*good,f.committee.members[0].peer,*f.keys[0]); }),"failed durable storage exposes no vote");
    const auto finalized = a.finalize(*good,proposal);
    check(finalized && finalized->attestations.size() == 3 && a.accept(p,verify,*finalized).has_value(),"three of four members finalize with one unavailable");
    bool disk_failure = true;
    Agreement transactional(f.committee,{},0,[&](const Bytes&){ if (disk_failure) throw std::runtime_error("disk unavailable"); });
    check(refused([&]{ transactional.accept(p,verify,*finalized); }) && transactional.next_tick() == 0 && transactional.previous() == Digest{},"failed finalization flush retains the live predecessor");
    disk_failure = false;
    check(transactional.accept(p,verify,*finalized).has_value() && transactional.next_tick() == 1,"verified finalization retries after storage recovers without skipping a step");
    check(!a.accept(p,verify,*finalized),"replayed finalization has no effect");
    check(refused([&]{a.attest(*speculative,f.committee.members[0].peer,*f.keys[0]);}),"a known next tick still requires the exact finalized predecessor hash");
    auto alternate = *finalized; alternate.proposal = close; check(alternate.id() == finalized->id(),"receipt is independent of proposer and sufficient attestation subset");
    integrity.retire_before(1); check(integrity.receive(first,p.epoch.context.id(),0) == Delivery::Replay,"old signed observations have no effect");
    auto late = first; late.payload = {9}; late.sign(*f.keys[0]);
    check(integrity.receive(late,p.epoch.context.id(),0) == Delivery::Equivocation && integrity.evidence().size() == 2,"late conflicts preserve signed evidence without reopening a finalized step");
    auto changed_topology = p; changed_topology.declared_topology = {42};
    check(!verify.proposal(changed_topology,f.committee,proposal) && topology_hash(p.layout,{1}) != topology_hash(p.layout,{2}),"named relation changes cannot alias an otherwise identical numerical topology");
    auto unsafe = f.committee; unsafe.quorum = 2; check(refused([&]{ unsafe.validate(); }),"quorums must intersect beyond the fault budget");
    unsafe = f.committee; unsafe.members[1].key = unsafe.members[0].key; check(refused([&]{ unsafe.validate(); }),"one public key cannot count as independent executors");
    PublicKey identity_key{}; identity_key[0] = 1; Signature identity_signature{}; identity_signature[0] = 1;
    unsafe = f.committee; unsafe.members[0].key = identity_key;
    check(refused([&]{ unsafe.validate(); }) && !check_signature(identity_key,first.statement(),identity_signature),"small-order identity keys cannot forge observations or quorum membership");
    auto event_inputs = p.epoch.ordered_inputs;
    for (int i = 0; i < 2; ++i) { event_inputs[i].discrete = true; event_inputs[i].conflict = "inventory:item"; event_inputs[i].sign(*f.keys[i]); }
    const auto events = Problem::make(p.epoch.context,p.system(),p.solver,event_inputs);
    Verify event_verify([](const Observation& o){ return o.payload.size() == 1; });
    check(refused([&]{ event_verify.propose(events,f.committee,f.committee.members[0].peer,*f.keys[0],cpu); }),"conflicting discrete operations cannot both finalize");
    Fixture replacement(11); replacement.committee.generation = 1; Handoff h{*finalized,good->checkpoint(),replacement.committee,{}};
    for (int i = 0; i < 3; ++i) h.endorsements.push_back(a.endorse(p,verify,h,f.committee.members[i].peer,*f.keys[i]));
    check(verify_handoff(p,f.committee,verify,h),"new committee verifies exact checkpoint and old quorum handoff");
    auto fork = h; fork.next.members[0].peer = "replacement";
    check(refused([&]{ a.endorse(p,verify,fork,f.committee.members[0].peer,*f.keys[0]); }),"one finalized generation cannot endorse conflicting replacements");
    auto corrupted = h; corrupted.checkpoint[0] ^= 1; check(!verify_handoff(p,f.committee,verify,corrupted),"corrupted handoff is rejected");
    const auto next_context = replacement.context(finalized->id(),1);
    const auto next = Problem::make(next_context,replacement.system(),replacement.spec,replacement.inputs(next_context));
    check(verify.propose(next,replacement.committee,replacement.committee.members[0].peer,*replacement.keys[0],cpu).certificate.result == proposal.certificate.result,"replacing every machine preserves finalized world values");
    auto replacement_agreement = Agreement::resume(p,f.committee,verify,h);
    const auto new_proposal = verify.propose(next,replacement.committee,replacement.committee.members[0].peer,*replacement.keys[0],cpu);
    const auto new_verified = verify.proposal(next,replacement.committee,new_proposal);
    auto wrong_world_context = next_context; wrong_world_context.world = "different-world";
    const auto wrong_world = Problem::make(wrong_world_context,replacement.system(),replacement.spec,replacement.inputs(wrong_world_context));
    const auto wrong_world_proposal = verify.propose(wrong_world,replacement.committee,replacement.committee.members[0].peer,*replacement.keys[0],cpu);
    const auto wrong_world_verified = verify.proposal(wrong_world,replacement.committee,wrong_world_proposal);
    check(refused([&]{ replacement_agreement.attest(*wrong_world_verified,replacement.committee.members[0].peer,*replacement.keys[0]); }),"handoff retains world identity as well as finalized values");
    for (int i = 0; i < 3; ++i) replacement_agreement.attest(*new_verified,replacement.committee.members[i].peer,*replacement.keys[i]);
    const auto new_final = replacement_agreement.finalize(*new_verified,new_proposal);
    check(new_final && replacement_agreement.accept(next,verify,*new_final) && replacement_agreement.next_tick() == 2,"all-new committee finalizes the next exact world step");
    auto old_context = f.context(finalized->id(),1); const auto old_next = Problem::make(old_context,f.system(),f.spec,f.inputs(old_context));
    const auto old_proposal = verify.propose(old_next,f.committee,f.committee.members[0].peer,*f.keys[0],cpu);
    const auto old_verified = verify.proposal(old_next,f.committee,old_proposal);
    check(refused([&]{ a.attest(*old_verified,f.committee.members[0].peer,*f.keys[0]); }),"endorsing a handoff seals the old generation against further votes");
    auto pinned_system = f.system(); pinned_system.fixed[0] = 1; pinned_system.pins[0] = 2.345678901234;
    auto pinned_context = f.context(); pinned_context.constraints = constraint_hash(pinned_system,{});
    const auto pinned = Problem::make(pinned_context,pinned_system,f.spec,f.inputs(pinned_context));
    check(verify.propose(pinned,f.committee,f.committee.members[0].peer,*f.keys[0],cpu).values[0] == pinned_system.pins[0],"off-grid hard pins remain exact");
    Prediction prediction([](const PredictedValues& x,const DiscreteInputs& input){ return PredictedValues{x[0]+input[0],x[1]+1}; },4,16,{1,0});
    prediction.reset(0,{0,0}); prediction.advance({1}); prediction.advance({1}); prediction.advance({1}); const auto correction = prediction.reconcile(1,{0,1},0.05);
    check(correction.applied && correction.replayed == 2 && prediction.predicted() == PredictedValues({2,3}),"late finalization rolls back and replays speculative discrete inputs");
    check(prediction.display(0.05)[0] == 3 && prediction.display(0.2)[0] == 2 && prediction.display(0.05)[1] == 3,"smoothing uses Temporal time and excludes discrete data");
    check(prediction.amend(2,{-1},0.2) && prediction.predicted() == PredictedValues({0,3}),"late boundary input repairs a disposable forecast");
    check(!prediction.amend(1,{9},0.2) && !prediction.reconcile(1,{9,9},0.2).applied,"old input cannot change finalized anchors");
    check(prediction.advance({1}) && prediction.advance({1}) && !prediction.advance({1}) && prediction.sample(1) == std::optional<PredictedValues>{{0,1}},"lead is bounded and historical compensation queries are read-only");
    check(!prediction.finalized_sample(2) && prediction.finalized_sample(1) == std::optional<PredictedValues>{{0,1}},"latency validation history excludes speculative frames");
    prediction.reconcile(4,{5,4},0.3);
    check(!prediction.finalized_sample(2) && !prediction.finalized_sample(3) && prediction.finalized_sample(4) == std::optional<PredictedValues>{{5,4}},"skipped speculative frames cannot masquerade as finalized history");
    Message wire; wire.from = f.committee.members[0].peer; wire.to = f.committee.members[1].peer; wire.kind = MessageKind::Finalization; wire.inputs = p.epoch.ordered_inputs; wire.finalization = *finalized;
    check(encode(decode_message(encode(wire))) == encode(wire),"signed regional wire encoding is canonical");
    Message vote_wire = wire; vote_wire.kind = MessageKind::Attestation; vote_wire.proposal = proposal; vote_wire.attestation = finalized->attestations[0];
    const auto small_vote = encode(vote_wire).size(), small_final = encode(wire).size();
    Message data_wire = wire; data_wire.kind = MessageKind::Proposal; data_wire.proposal = proposal;
    const auto small_data = encode(data_wire).size();
    vote_wire.proposal.values.resize(4096,6); wire.finalization.proposal.values.resize(4096,6); data_wire.proposal.values.resize(4096,6);
    check(encode(vote_wire).size() == small_vote && encode(wire).size() == small_final &&
          encode(data_wire).size() == small_data+8*(4096-2) && decode_message(encode(wire)).finalization.proposal.values.empty(),
          "attestation/finalization bytes exclude coordinates and inputs; only full proposal bytes grow with the solved vector");
    // Isolate the fourth peer until the other three finalize. Deliver only a
    // compact receipt first; it must explicitly recover authenticated data.
    std::vector<std::unique_ptr<Agreement>> agreements;
    std::vector<std::unique_ptr<Integrity>> integrities;
    std::vector<std::unique_ptr<Protocol>> protocols;
    std::vector<InputSlot> slots;
    for (const auto& o : p.epoch.ordered_inputs) slots.push_back({o.peer,o.object,o.parameter,o.sequence,o.discrete});
    const auto builder = [&](const std::vector<Observation>& inputs){ return Problem::make(p.epoch.context,p.system(),p.solver,inputs); };
    for (int i = 0; i < 4; ++i) {
        agreements.push_back(std::make_unique<Agreement>(f.committee)); integrities.push_back(std::make_unique<Integrity>(f.committee));
        protocols.push_back(std::make_unique<Protocol>(f.committee,f.committee.members[i].peer,*f.keys[i],cpu,p.epoch.context,slots,builder,Verify{},*agreements.back(),*integrities.back()));
    }
    for (int i = 0; i < 2; ++i) protocols[i]->submit({p.epoch.ordered_inputs[i]});
    std::optional<Message> receipt;
    for (int round = 0; round < 12; ++round) {
        std::vector<Message> messages;
        for (auto& protocol : protocols) { auto out = protocol->outgoing(); messages.insert(messages.end(),out.begin(),out.end()); }
        std::reverse(messages.begin(),messages.end());
        for (const auto& message : messages) {
            if (message.to == f.committee.members[3].peer) { if (message.kind == MessageKind::Finalization) receipt = message; continue; }
            const auto target = std::find_if(f.committee.members.begin(),f.committee.members.end(),[&](const auto& member){return member.peer == message.to;});
            const auto index = static_cast<std::size_t>(target-f.committee.members.begin());
            protocols[index]->receive(decode_message(encode(message))); protocols[index]->receive(decode_message(encode(message)));
        }
    }
    check(receipt && protocols[0]->accepted() && protocols[1]->accepted() && protocols[2]->accepted() && !protocols[3]->accepted(),"compact reordered/duplicate control traffic finalizes a quorum with one disconnected peer");
    if (receipt) {
        auto forged_reference = *receipt; forged_reference.finalization.proposal.certificate.signature[0] ^= 1;
        protocols[3]->receive(forged_reference);
        check(protocols[3]->outgoing().empty() && !protocols[3]->accepted(),"forged references cannot trigger data recovery or world acceptance");
        auto unknown = *receipt; auto& cert = unknown.finalization.proposal.certificate; cert.result[0] ^= 1;
        const auto signer = std::find_if(f.committee.members.begin(),f.committee.members.end(),[&](const auto& member){return member.peer == cert.signer;});
        cert.signature = f.keys[static_cast<std::size_t>(signer-f.committee.members.begin())]->sign(cert.statement());
        protocols[3]->receive(unknown); protocols[3]->receive(*receipt);
        check(!protocols[3]->accepted() && !protocols[3]->problem(),"unknown digest references wait for authenticated full data instead of inventing inputs");
        unsigned responses = 0;
        for (const auto& request : protocols[3]->outgoing()) {
            const auto target = std::find_if(f.committee.members.begin(),f.committee.members.end(),[&](const auto& member){return member.peer == request.to;});
            auto& sender = *protocols[static_cast<std::size_t>(target-f.committee.members.begin())];
            auto forged_request = request; forged_request.attestation.signature[0] ^= 1; sender.receive(forged_request);
            sender.receive(decode_message(encode(request)));
            for (const auto& response : sender.outgoing()) if (response.to == f.committee.members[3].peer && response.kind == MessageKind::Proposal) {
                ++responses; protocols[3]->receive(decode_message(encode(response)));
            }
        }
        check(responses == 1 && protocols[3]->accepted() && protocols[3]->finalized()->id() == protocols[0]->finalized()->id(),"signed bounded request/retransmission recovers a missing proposal after finalization with the identical receipt");
    }
    bool one_reference = true;
    for (const auto& protocol : protocols) { const auto stats = protocol->verification_diagnostics(); one_reference &= stats.reference_solves == 1 && stats.authenticated_input_sets == 1; }
    check(one_reference,"every active peer independently solves/authenticates its immutable epoch exactly once across all protocol messages");
    for (unsigned tick = 1; tick <= 2; ++tick) {
        const auto context = f.context(hash(Bytes{static_cast<std::uint8_t>(tick)}),tick);
        const auto next_problem = Problem::make(context,f.system(),f.spec,f.inputs(context));
        cached.propose(next_problem,f.committee,f.committee.members[0].peer,*f.keys[0],cpu);
    }
    cached.proposal(p,f.committee,cached_proposal);
    check(cached.diagnostics().reference_solves == 4,"reference cache is bounded to two problems and evicted epochs require a fresh independent solve");
    auto truncated = encode(wire); truncated.pop_back(); check(refused([&]{ decode_message(truncated); }),"truncated signed frame is rejected");
    return failures ? 1 : 0;
}
