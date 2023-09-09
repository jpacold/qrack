//////////////////////////////////////////////////////////////////////////////////////
//
// (C) Daniel Strano and the Qrack contributors 2017-2023. All rights reserved.
//
// QTensorNetwork is a gate-based QInterface descendant wrapping cuQuantum.
//
// Licensed under the GNU Lesser General Public License V3.
// See LICENSE.md in the project root or https://www.gnu.org/licenses/lgpl-3.0.en.html
// for details.

#pragma once

#include "qcircuit.hpp"

#if ENABLE_QUNIT_CPU_PARALLEL && ENABLE_PTHREAD
#include "common/dispatchqueue.hpp"
#endif

namespace Qrack {

class QTensorNetwork;
typedef std::shared_ptr<QTensorNetwork> QTensorNetworkPtr;

#if ENABLE_CUDA
struct TensorMeta {
    std::vector<std::vector<int32_t>> modes;
    std::vector<std::vector<int64_t>> extents;
};
typedef std::vector<TensorMeta> TensorNetworkMeta;
typedef std::shared_ptr<TensorNetworkMeta> TensorNetworkMetaPtr;
#endif

class QTensorNetwork : public QInterface {
protected:
    bool useHostRam;
    bool isSparse;
    bool isReactiveSeparate;
    bool useTGadget;
    int64_t devID;
    QInterfacePtr layerStack;
    std::vector<int64_t> deviceIDs;
    std::vector<QInterfaceEngine> engines;
    std::vector<QCircuitPtr> circuit;
    std::vector<std::map<bitLenInt, bool>> measurements;
#if ENABLE_QUNIT_CPU_PARALLEL && ENABLE_PTHREAD
    DispatchQueue dispatchQueue;
#endif

    void Dispatch(DispatchFn fn)
    {
#if ENABLE_QUNIT_CPU_PARALLEL && ENABLE_PTHREAD
        dispatchQueue.dispatch(fn);
#else
        fn();
#endif
    }

    QCircuitPtr GetCircuit(bitLenInt target, std::vector<bitLenInt> controls = std::vector<bitLenInt>())
    {
        for (size_t i = 0U; i < measurements.size(); ++i) {
            size_t l = measurements.size() - (i + 1U);
            std::map<bitLenInt, bool>& m = measurements[l];
            ++l;

            if (m.find(target) != m.end()) {
                if (circuit.size() == l) {
                    circuit.push_back(std::make_shared<QCircuit>());
                }

                return circuit[l];
            }

            for (size_t j = 0U; j < controls.size(); ++j) {
                if (m.find(controls[j]) != m.end()) {
                    if (circuit.size() == l) {
                        circuit.push_back(std::make_shared<QCircuit>());
                    }

                    return circuit[l];
                }
            }
        }

        return circuit[0U];
    }

    void CheckQubitCount(bitLenInt target)
    {
        ++target;
        if (target > qubitCount) {
            SetQubitCount(target);
        }
    }

    void CheckQubitCount(bitLenInt target, const std::vector<bitLenInt>& controls)
    {
        CheckQubitCount(target);
        for (const bitLenInt& c : controls) {
            if ((c + 1U) > qubitCount) {
                SetQubitCount(c + 1U);
            }
        }
    }

    void RunMeasurmentLayer(size_t layerId)
    {
        const std::map<bitLenInt, bool>& mLayer = measurements[layerId];
        std::vector<bitLenInt> bits;
        bits.reserve(mLayer.size());
        std::vector<bool> values;
        values.reserve(mLayer.size());

        for (const auto& m : mLayer) {
            bits.push_back(m.first);
            values.push_back(m.second);
        }

        layerStack->ForceM(bits, values);
    }

    bitLenInt GetThresholdQb()
    {
#if ENABLE_ENV_VARS
        return getenv("QRACK_QTENSORNETWORK_THRESHOLD_QB")
            ? (bitLenInt)std::stoi(std::string(getenv("QRACK_QTENSORNETWORK_THRESHOLD_QB")))
            : 27U;
#else
        return 27U;
#endif
    }

    void MakeLayerStack(std::set<bitLenInt> qubits = std::set<bitLenInt>());

    template <typename Fn> void RunAsAmplitudes(Fn fn, const std::set<bitLenInt>& qubits = std::set<bitLenInt>())
    {
        Finish();

        if (!qubits.size()) {
            MakeLayerStack();
            return fn(layerStack);
        }

        const bitLenInt maxQb = GetThresholdQb();
        if (qubitCount <= maxQb) {
            MakeLayerStack();
            return fn(layerStack);
        } else {
            // #if ENABLE_CUDA
            // TODO: Calculate result of measurement with cuTensorNetwork
            // TensorNetworkMetaPtr network = MakeTensorNetwork();
            // throw std::runtime_error("QTensorNetwork doesn't have cuTensorNetwork capabilities yet!");
            // #else
            MakeLayerStack(qubits);
            QInterfacePtr ls = layerStack;
            layerStack = NULL;
            return fn(ls);
            // #endif
        }
    }

#if ENABLE_CUDA
    TensorNetworkMetaPtr MakeTensorNetwork() { return NULL; }
#endif

public:
    QTensorNetwork(std::vector<QInterfaceEngine> eng, bitLenInt qBitCount, bitCapInt initState = 0,
        qrack_rand_gen_ptr rgp = nullptr, complex phaseFac = CMPLX_DEFAULT_ARG, bool doNorm = false,
        bool randomGlobalPhase = true, bool useHostMem = false, int64_t deviceId = -1, bool useHardwareRNG = true,
        bool useSparseStateVec = false, real1_f norm_thresh = REAL1_EPSILON, std::vector<int64_t> ignored = {},
        bitLenInt qubitThreshold = 0, real1_f separation_thresh = FP_NORM_EPSILON_F);

    QTensorNetwork(bitLenInt qBitCount, bitCapInt initState = 0U, qrack_rand_gen_ptr rgp = nullptr,
        complex phaseFac = CMPLX_DEFAULT_ARG, bool doNorm = false, bool randomGlobalPhase = true,
        bool useHostMem = false, int64_t deviceId = -1, bool useHardwareRNG = true, bool useSparseStateVec = false,
        real1_f norm_thresh = REAL1_EPSILON, std::vector<int64_t> devList = {}, bitLenInt qubitThreshold = 0U,
        real1_f separation_thresh = FP_NORM_EPSILON_F)
        : QTensorNetwork({}, qBitCount, initState, rgp, phaseFac, doNorm, randomGlobalPhase, useHostMem, deviceId,
              useHardwareRNG, useSparseStateVec, norm_thresh, devList, qubitThreshold, separation_thresh)
    {
    }

    ~QTensorNetwork() { Dump(); }

    void SetDevice(int64_t dID) { devID = dID; }

    void Finish()
    {
#if ENABLE_QUNIT_CPU_PARALLEL && ENABLE_PTHREAD
        dispatchQueue.finish();
#endif
    };

    bool isFinished()
    {
#if ENABLE_QUNIT_CPU_PARALLEL && ENABLE_PTHREAD
        return dispatchQueue.isFinished();
#else
        return true;
#endif
    }

    void Dump()
    {
#if ENABLE_QUNIT_CPU_PARALLEL && ENABLE_PTHREAD
        dispatchQueue.dump();
#endif
    }

    void UpdateRunningNorm(real1_f norm_thresh = REAL1_DEFAULT_ARG)
    {
        // Intentionally left blank.
    }

    void NormalizeState(
        real1_f nrm = REAL1_DEFAULT_ARG, real1_f norm_thresh = REAL1_DEFAULT_ARG, real1_f phaseArg = ZERO_R1_F)
    {
        // Intentionally left blank
    }

    real1_f SumSqrDiff(QInterfacePtr toCompare)
    {
        return SumSqrDiff(std::dynamic_pointer_cast<QTensorNetwork>(toCompare));
    }
    real1_f SumSqrDiff(QTensorNetworkPtr toCompare)
    {
        real1_f toRet;
        toCompare->MakeLayerStack();
        RunAsAmplitudes([&](QInterfacePtr ls) { toRet = ls->SumSqrDiff(toCompare->layerStack); });
        return toRet;
    }

    void SetPermutation(bitCapInt initState, complex phaseFac = CMPLX_DEFAULT_ARG)
    {
        Dump();
        circuit.clear();
        measurements.clear();
        layerStack = NULL;

        circuit.push_back(std::make_shared<QCircuit>());

        for (bitLenInt i = 0U; i < qubitCount; ++i) {
            if (initState & pow2(i)) {
                X(i);
            }
        }

        if (phaseFac == CMPLX_DEFAULT_ARG) {
            if (randGlobalPhase) {
                real1_f angle = Rand() * 2 * (real1_f)PI_R1;
                phaseFac = complex((real1)cos(angle), (real1)sin(angle));
            } else {
                return;
            }
        }

        Phase(phaseFac, phaseFac, 0U);
    }

    QInterfacePtr Clone();

    void GetQuantumState(complex* state)
    {
        RunAsAmplitudes([&](QInterfacePtr ls) { ls->GetQuantumState(state); });
    }
    void SetQuantumState(const complex* state)
    {
        throw std::domain_error("QTensorNetwork::SetQuantumState() not implemented!");
    }
    void SetQuantumState(QInterfacePtr eng)
    {
        throw std::domain_error("QTensorNetwork::SetQuantumState() not implemented!");
    }
    void GetProbs(real1* outputProbs)
    {
        RunAsAmplitudes([&](QInterfacePtr ls) { ls->GetProbs(outputProbs); });
    }

    complex GetAmplitude(bitCapInt perm)
    {
        complex toRet;
        RunAsAmplitudes([&](QInterfacePtr ls) { toRet = ls->GetAmplitude(perm); });
        return toRet;
    }
    void SetAmplitude(bitCapInt perm, complex amp)
    {
        throw std::domain_error("QTensorNetwork::SetAmplitude() not implemented!");
    }

    using QInterface::Compose;
    bitLenInt Compose(QInterfacePtr toCopy, bitLenInt start)
    {
        throw std::domain_error("QTensorNetwork::Compose() not implemented!");
    }
    void Decompose(bitLenInt start, QInterfacePtr dest)
    {
        throw std::domain_error("QTensorNetwork::Decompose() not implemented!");
    }
    QInterfacePtr Decompose(bitLenInt start, bitLenInt length)
    {
        throw std::domain_error("QTensorNetwork::Decompose() not implemented!");
    }
    void Dispose(bitLenInt start, bitLenInt length)
    {
        throw std::domain_error("QTensorNetwork::Dispose() not implemented!");
    }
    void Dispose(bitLenInt start, bitLenInt length, bitCapInt disposedPerm)
    {
        throw std::domain_error("QTensorNetwork::Dispose() not implemented!");
    }

    using QInterface::Allocate;
    bitLenInt Allocate(bitLenInt start, bitLenInt length)
    {
        if (start > qubitCount) {
            throw std::invalid_argument("QTensorNetwork::Allocate() 'start' argument is out-of-bounds!");
        }

        const bitLenInt movedQubits = qubitCount - start;
        SetQubitCount(qubitCount + length);
        if (!movedQubits) {
            return start;
        }

        for (bitLenInt i = 0U; i < movedQubits; ++i) {
            const bitLenInt q = start + movedQubits - (i + 1U);
            Swap(q, q + length);
        }

        return start;
    }

    real1_f Prob(bitLenInt qubit)
    {
        real1_f toRet;
        RunAsAmplitudes([&](QInterfacePtr ls) { toRet = ls->Prob(qubit); }, { qubit });
        return toRet;
    }
    real1_f ProbAll(bitCapInt fullRegister)
    {
        real1_f toRet;
        RunAsAmplitudes([&](QInterfacePtr ls) { toRet = ls->ProbAll(fullRegister); });
        return toRet;
    }

    bool ForceM(bitLenInt qubit, bool result, bool doForce = true, bool doApply = true);

    bitCapInt MAll()
    {
        bitCapInt toRet = 0U;

        const bitLenInt maxQb = GetThresholdQb();
        if (qubitCount <= maxQb) {
            MakeLayerStack();
            toRet = layerStack->MAll();
        } else {
            for (bitLenInt i = 0U; i < qubitCount; ++i) {
                if (M(i)) {
                    toRet |= pow2(i);
                }
            }
        }

        SetPermutation(toRet);

        return toRet;
    }

    std::map<bitCapInt, int> MultiShotMeasureMask(const std::vector<bitCapInt>& qPowers, unsigned shots)
    {
        std::set<bitLenInt> qubits;
        for (const bitCapInt& qPow : qPowers) {
            qubits.insert(log2(qPow));
        }
        std::map<bitCapInt, int> toRet;
        RunAsAmplitudes([&](QInterfacePtr ls) { toRet = ls->MultiShotMeasureMask(qPowers, shots); }, qubits);

        return toRet;
    }
    void MultiShotMeasureMask(const std::vector<bitCapInt>& qPowers, unsigned shots, unsigned long long* shotsArray)
    {
        std::set<bitLenInt> qubits;
        for (const bitCapInt& qPow : qPowers) {
            qubits.insert(log2(qPow));
        }
        RunAsAmplitudes([&](QInterfacePtr ls) { ls->MultiShotMeasureMask(qPowers, shots, shotsArray); }, qubits);
    }

    void Mtrx(const complex* mtrx, bitLenInt target)
    {
        layerStack = NULL;
        CheckQubitCount(target);
        std::shared_ptr<complex> lMtrx(new complex[4U], std::default_delete<complex[]>());
        std::copy(mtrx, mtrx + 4U, lMtrx.get());
        Dispatch([this, target, lMtrx] {
            GetCircuit(target)->AppendGate(std::make_shared<QCircuitGate>(target, lMtrx.get()));
        });
    }
    void MCMtrx(const std::vector<bitLenInt>& ctrls, const complex* mtrx, bitLenInt target)
    {
        layerStack = NULL;
        std::vector<bitLenInt> controls(ctrls);
        CheckQubitCount(target, controls);
        std::shared_ptr<complex> lMtrx(new complex[4U], std::default_delete<complex[]>());
        std::copy(mtrx, mtrx + 4U, lMtrx.get());
        Dispatch([this, target, controls, lMtrx] {
            GetCircuit(target, controls)
                ->AppendGate(std::make_shared<QCircuitGate>(target, lMtrx.get(),
                    std::set<bitLenInt>{ controls.begin(), controls.end() }, pow2(controls.size()) - 1U));
        });
    }
    void MACMtrx(const std::vector<bitLenInt>& ctrls, const complex* mtrx, bitLenInt target)
    {
        layerStack = NULL;
        std::vector<bitLenInt> controls(ctrls);
        CheckQubitCount(target, controls);
        std::shared_ptr<complex> lMtrx(new complex[4U], std::default_delete<complex[]>());
        std::copy(mtrx, mtrx + 4U, lMtrx.get());
        Dispatch([this, target, controls, lMtrx] {
            GetCircuit(target, controls)
                ->AppendGate(std::make_shared<QCircuitGate>(
                    target, lMtrx.get(), std::set<bitLenInt>{ controls.begin(), controls.end() }, 0U));
        });
    }
    void MCPhase(const std::vector<bitLenInt>& ctrls, complex topLeft, complex bottomRight, bitLenInt target)
    {
        layerStack = NULL;
        std::vector<bitLenInt> controls(ctrls);
        CheckQubitCount(target, controls);
        std::shared_ptr<complex> lMtrx(new complex[4U], std::default_delete<complex[]>());
        lMtrx.get()[0U] = topLeft;
        lMtrx.get()[1U] = ZERO_CMPLX;
        lMtrx.get()[2U] = ZERO_CMPLX;
        lMtrx.get()[3U] = bottomRight;
        Dispatch([this, target, controls, lMtrx] {
            GetCircuit(target, controls)
                ->AppendGate(std::make_shared<QCircuitGate>(target, lMtrx.get(),
                    std::set<bitLenInt>{ controls.begin(), controls.end() }, pow2(controls.size()) - 1U));
        });
    }
    void MACPhase(const std::vector<bitLenInt>& ctrls, complex topLeft, complex bottomRight, bitLenInt target)
    {
        layerStack = NULL;
        std::vector<bitLenInt> controls(ctrls);
        CheckQubitCount(target, controls);
        std::shared_ptr<complex> lMtrx(new complex[4U], std::default_delete<complex[]>());
        lMtrx.get()[0U] = topLeft;
        lMtrx.get()[1U] = ZERO_CMPLX;
        lMtrx.get()[2U] = ZERO_CMPLX;
        lMtrx.get()[3U] = bottomRight;
        Dispatch([this, target, controls, lMtrx] {
            GetCircuit(target, controls)
                ->AppendGate(std::make_shared<QCircuitGate>(
                    target, lMtrx.get(), std::set<bitLenInt>{ controls.begin(), controls.end() }, 0U));
        });
    }
    void MCInvert(const std::vector<bitLenInt>& ctrls, complex topRight, complex bottomLeft, bitLenInt target)
    {
        layerStack = NULL;
        std::vector<bitLenInt> controls(ctrls);
        CheckQubitCount(target, controls);
        std::shared_ptr<complex> lMtrx(new complex[4U], std::default_delete<complex[]>());
        lMtrx.get()[0U] = ZERO_CMPLX;
        lMtrx.get()[1U] = topRight;
        lMtrx.get()[2U] = bottomLeft;
        lMtrx.get()[3U] = ZERO_CMPLX;
        Dispatch([this, target, controls, lMtrx] {
            GetCircuit(target, controls)
                ->AppendGate(std::make_shared<QCircuitGate>(target, lMtrx.get(),
                    std::set<bitLenInt>{ controls.begin(), controls.end() }, pow2(controls.size()) - 1U));
        });
    }
    void MACInvert(const std::vector<bitLenInt>& ctrls, complex topRight, complex bottomLeft, bitLenInt target)
    {
        layerStack = NULL;
        std::vector<bitLenInt> controls(ctrls);
        CheckQubitCount(target, controls);
        std::shared_ptr<complex> lMtrx(new complex[4U], std::default_delete<complex[]>());
        lMtrx.get()[0U] = ZERO_CMPLX;
        lMtrx.get()[1U] = topRight;
        lMtrx.get()[2U] = bottomLeft;
        lMtrx.get()[3U] = ZERO_CMPLX;
        Dispatch([this, target, controls, lMtrx] {
            GetCircuit(target, controls)
                ->AppendGate(std::make_shared<QCircuitGate>(
                    target, lMtrx.get(), std::set<bitLenInt>{ controls.begin(), controls.end() }, 0U));
        });
    }

    void FSim(real1_f theta, real1_f phi, bitLenInt qubit1, bitLenInt qubit2);
};
} // namespace Qrack
