//
// <copyright file="ConvolutionEngine.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//

#include "stdafx.h"
#include "ConvolutionEngine.h"
#include "CuDnnConvolutionEngine.h"

namespace Microsoft { namespace MSR { namespace CNTK {

template<class ElemType>
class DefaultConvolutionEngine : public ConvolutionEngine<ElemType>
{
public:
    DefaultConvolutionEngine(DEVICEID_TYPE deviceId, size_t maxTempMemSizeInSamples)
        : m_tempMatrix(deviceId), m_maxTempMemSizeInSamples(maxTempMemSizeInSamples)
    {
    }

public:
    void Forward(const Tensor4D& inT, const Mat& in, const Tensor4D& filterT, const Mat& filter, const ConvolutionOptions& convOpt,
        const Tensor4D& outT, Mat& out) override
    {
        size_t packedInputRows = filterT.w() * filterT.h() * inT.c();
        size_t packedInputColsPerSample = outT.w() * outT.h();
        size_t outputSizePerChannel = packedInputColsPerSample;

        assert(inT.n() == in.GetNumCols());
        assert(outT.n() == out.GetNumCols());

        size_t batchSize = inT.n();
        size_t maxTempMemSizeInSamples = (m_maxTempMemSizeInSamples == 0 ? batchSize : m_maxTempMemSizeInSamples);

        assert(filter.GetNumCols() == packedInputRows && filter.GetNumRows() == outT.c());
        out.Resize(outT.c(), outputSizePerChannel * batchSize);

        size_t subBatchSize = min(batchSize, maxTempMemSizeInSamples);
        size_t numSubBatches = (batchSize + subBatchSize - 1) / subBatchSize;

        for (size_t i = 0; i < numSubBatches; i++)
        {
            size_t startSampleId = i * subBatchSize;
            size_t endSampleId = min(batchSize, startSampleId + subBatchSize);
            size_t smallBatchSize = endSampleId - startSampleId;

            m_tempMatrix.Resize(packedInputRows, packedInputColsPerSample * smallBatchSize);
            Mat inputSubBatch = in.ColumnSlice(startSampleId, smallBatchSize);
            m_tempMatrix.AssignPackedConvolutionInput(inputSubBatch,
                inT.w(), inT.h(), inT.c(),
                outT.w(), outT.h(), outT.c(),
                filterT.w(), filterT.h(), convOpt.wStride(), convOpt.hStride(),
                convOpt.padding());

            Mat outputSubBatch = out.ColumnSlice(outputSizePerChannel * startSampleId, outputSizePerChannel * smallBatchSize);
            Mat::Multiply(filter, false, m_tempMatrix, false, outputSubBatch);
        }

        out.Reshape(outT.c() * outputSizePerChannel, batchSize);  //each sample becomes a column
    }

    Tensor4DPtr CreateTensor(size_t w, size_t h, size_t c, size_t n) override
    {
        return std::make_unique<ConvolutionTensor4D>(w, h, c, n);
    }

private:
    size_t m_maxTempMemSizeInSamples;
    Mat m_tempMatrix;
};

template<class ElemType>
std::unique_ptr<ConvolutionEngine<ElemType>> ConvolutionEngine<ElemType>::Create(DEVICEID_TYPE deviceId, size_t maxTempMemSizeInSamples)
{
    // REVIEW alexeyk: on GPU make cuDNN default, add config parameter to control that.
    if (deviceId >= 0 && CuDnnConvolutionEngine<ElemType>::IsSupported())
        return std::make_unique<CuDnnConvolutionEngine<ElemType>>(deviceId, maxTempMemSizeInSamples);
    return std::make_unique<DefaultConvolutionEngine<ElemType>>(deviceId, maxTempMemSizeInSamples);
}

template class ConvolutionEngine<float>;
template class ConvolutionEngine<double>;
}}}