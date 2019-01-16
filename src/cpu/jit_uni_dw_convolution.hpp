/*******************************************************************************
* Copyright 2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef CPU_JIT_UNI_DW_CONVOLUTION_HPP
#define CPU_JIT_UNI_DW_CONVOLUTION_HPP

#include "c_types_map.hpp"
#include "memory_tracking.hpp"

#include "cpu_barrier.hpp"
#include "cpu_convolution_pd.hpp"
#include "cpu_primitive.hpp"
#include "cpu_reducer.hpp"

#include "jit_uni_dw_conv_kernel_f32.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

template <cpu_isa_t isa>
struct _jit_uni_dw_convolution_fwd_t: public cpu_primitive_t {
    struct pd_t: public cpu_convolution_fwd_pd_t {
        pd_t(engine_t *engine, const convolution_desc_t *adesc,
                const primitive_attr_t *attr,
                const typename pd_t::base_class *hint_fwd_pd)
            : cpu_convolution_fwd_pd_t(engine, adesc, attr, hint_fwd_pd)
            , jcp_() {}

        DECLARE_COMMON_PD_T(
                JIT_IMPL_NAME_HELPER("jit_dw:", isa, ""),
                _jit_uni_dw_convolution_fwd_t<isa>);

        status_t init() {
            bool ok = true
                && is_fwd()
                && this->set_default_alg_kind(alg_kind::convolution_direct)
                && this->expect_data_types(data_type::f32, data_type::f32,
                        data_type::f32, data_type::f32, data_type::f32)
                && this->set_default_params() == status::success
                && !this->has_zero_dim_memory();

            if (!ok) return status::unimplemented;

            status_t status = jit_uni_dw_conv_fwd_kernel_f32<isa>::init_conf(
                    jcp_, *this->desc(), src_md(), *weights_md(), *dst_md(),
                    *this->attr());
            if (status != status::success) return status;

            auto scratchpad = scratchpad_registry().registrar();
            jit_uni_dw_conv_fwd_kernel_f32<isa>::init_scratchpad(scratchpad,
                    jcp_);

            return status::success;
        }

        jit_conv_conf_t jcp_;

    protected:
        status_t set_default_params() {
            using namespace memory_format;
            auto desired_act_fmt = isa == avx512_common ? nChw16c : nChw8c;
            auto desired_wei_fmt = isa == avx512_common ? Goihw16g : Goihw8g;

            if (src_md_.format == any)
                CHECK(types::set_default_format(src_md_, desired_act_fmt));
            if (dst_md_.format == any)
                CHECK(types::set_default_format(dst_md_, desired_act_fmt));
            if (weights_md_.format == any)
                CHECK(types::set_default_format(weights_md_, desired_wei_fmt));
            if (bias_md_.format == any)
                CHECK(types::set_default_format(bias_md_, x));
            return status::success;
        }
    };

    _jit_uni_dw_convolution_fwd_t(const pd_t *apd): cpu_primitive_t(apd)
    { kernel_ = new jit_uni_dw_conv_fwd_kernel_f32<isa>(pd()->jcp_); }

    ~_jit_uni_dw_convolution_fwd_t() { delete kernel_; }

    typedef typename prec_traits<data_type::f32>::type data_t;

    virtual status_t execute(const exec_ctx_t &ctx) const override {
        execute_forward(ctx);
        return status::success;
    }

private:
    void execute_forward(const exec_ctx_t &ctx) const;
    const pd_t *pd() const { return (const pd_t *)primitive_t::pd(); }

    jit_uni_dw_conv_fwd_kernel_f32<isa> *kernel_;
};

using jit_avx512_common_dw_convolution_fwd_t =
    _jit_uni_dw_convolution_fwd_t<avx512_common>;
using jit_avx2_dw_convolution_fwd_t = _jit_uni_dw_convolution_fwd_t<avx2>;
using jit_sse42_dw_convolution_fwd_t = _jit_uni_dw_convolution_fwd_t<sse42>;

template <cpu_isa_t isa>
struct _jit_uni_dw_convolution_bwd_data_t: public cpu_primitive_t {
    struct pd_t: public cpu_convolution_bwd_data_pd_t {
        pd_t(engine_t *engine,
                const convolution_desc_t *adesc,
                const primitive_attr_t *attr,
                const convolution_fwd_pd_t *hint_fwd_pd)
            : cpu_convolution_bwd_data_pd_t(engine, adesc, attr, hint_fwd_pd)
            , jcp_()
        {}

        DECLARE_COMMON_PD_T(
                JIT_IMPL_NAME_HELPER("jit_dw:", isa, ""),
                _jit_uni_dw_convolution_bwd_data_t);

        status_t init() {
            bool ok = true
                && this->desc()->prop_kind == prop_kind::backward_data
                && this->set_default_alg_kind(alg_kind::convolution_direct)
                && this->expect_data_types(data_type::f32, data_type::f32,
                        data_type::undef, data_type::f32, data_type::f32)
                && this->set_default_params() == status::success
                && !this->has_zero_dim_memory();

            if (!ok) return status::unimplemented;

            status_t status =
                jit_uni_dw_conv_bwd_data_kernel_f32<isa>::init_conf(jcp_,
                        *this->desc(), *diff_src_md(),
                        *weights_md(), *diff_dst_md());
            if (status != status::success) return status;

            auto scratchpad = scratchpad_registry().registrar();
            jit_uni_dw_conv_bwd_data_kernel_f32<isa>::init_scratchpad(
                    scratchpad, jcp_);

            return status::success;
        }

        jit_conv_conf_t jcp_;

    protected:
        status_t set_default_params() {
            using namespace memory_format;
            auto desired_act_fmt = isa == avx512_common ? nChw16c : nChw8c;
            auto desired_wei_fmt = isa == avx512_common ? Goihw16g : Goihw8g;

            if (diff_src_md_.format == any)
                CHECK(types::set_default_format(diff_src_md_, desired_act_fmt));
            if (diff_dst_md_.format == any)
                CHECK(types::set_default_format(diff_dst_md_, desired_act_fmt));
            if (weights_md_.format == any)
                CHECK(types::set_default_format(weights_md_, desired_wei_fmt));

            return status::success;
        }
    };

    _jit_uni_dw_convolution_bwd_data_t(const pd_t *apd): cpu_primitive_t(apd)
    { kernel_ = new jit_uni_dw_conv_bwd_data_kernel_f32<isa>(pd()->jcp_); }
    ~_jit_uni_dw_convolution_bwd_data_t() { delete kernel_; };

    typedef typename prec_traits<data_type::f32>::type data_t;

    virtual status_t execute(const exec_ctx_t &ctx) const override {
        execute_backward_data(ctx);
        return status::success;
    }

private:
    void execute_backward_data(const exec_ctx_t &ctx) const;
    const pd_t *pd() const { return (const pd_t *)primitive_t::pd(); }

    jit_uni_dw_conv_bwd_data_kernel_f32<isa> *kernel_;
};

using jit_avx512_common_dw_convolution_bwd_data_t =
    _jit_uni_dw_convolution_bwd_data_t<avx512_common>;
using jit_avx2_dw_convolution_bwd_data_t =
    _jit_uni_dw_convolution_bwd_data_t<avx2>;
using jit_sse42_dw_convolution_bwd_data_t =
    _jit_uni_dw_convolution_bwd_data_t<sse42>;

template <cpu_isa_t isa>
struct _jit_uni_dw_convolution_bwd_weights_t: public cpu_primitive_t {
    struct pd_t: public cpu_convolution_bwd_weights_pd_t {
        pd_t(engine_t *engine,
                const convolution_desc_t *adesc,
                const primitive_attr_t *attr,
                const convolution_fwd_pd_t *hint_fwd_pd)
            : cpu_convolution_bwd_weights_pd_t(engine, adesc, attr, hint_fwd_pd)
            , jcp_() {}

        DECLARE_COMMON_PD_T(
                JIT_IMPL_NAME_HELPER("jit_dw:", isa, ""),
                _jit_uni_dw_convolution_bwd_weights_t<isa>);

        status_t init() {
            bool ok = true
                && this->desc()->prop_kind == prop_kind::backward_weights
                && this->set_default_alg_kind(alg_kind::convolution_direct)
                && this->expect_data_types(data_type::f32, data_type::f32,
                        data_type::f32, data_type::f32, data_type::f32)
                && this->set_default_params() == status::success;

            if (!ok) return status::unimplemented;

            const int max_threads = mkldnn_in_parallel()
                ? 1 : mkldnn_get_max_threads();

            status_t status =
                jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::init_conf(jcp_,
                        *this->desc(), *src_md(),
                        *diff_weights_md(),
                        *diff_dst_md(), max_threads);
            if (status != status::success) return status;

            auto scratchpad = scratchpad_registry().registrar();
            jit_uni_dw_conv_bwd_weights_kernel_f32<isa>::init_scratchpad(
                    scratchpad, jcp_);

            return status::success;
        }

        jit_conv_conf_t jcp_;

    protected:
        status_t set_default_params() {
            using namespace memory_format;
            auto desired_act_fmt = isa == avx512_common ? nChw16c : nChw8c;
            auto desired_wei_fmt = isa == avx512_common ? Goihw16g : Goihw8g;

            if (src_md_.format == any)
                CHECK(types::set_default_format(src_md_, desired_act_fmt));
            if (diff_dst_md_.format == any)
                CHECK(types::set_default_format(diff_dst_md_, desired_act_fmt));
            if (diff_weights_md_.format == any)
                CHECK(types::set_default_format(diff_weights_md_, desired_wei_fmt));
            if (diff_bias_md_.format == any)
                CHECK(types::set_default_format(diff_bias_md_, x));

            return status::success;
        }
    };

    _jit_uni_dw_convolution_bwd_weights_t(const pd_t *apd);

    ~_jit_uni_dw_convolution_bwd_weights_t() {
        delete kernel_;
        delete acc_ker_;
    };

    typedef typename prec_traits<data_type::f32>::type data_t;

    virtual status_t execute(const exec_ctx_t &ctx) const override {
        execute_backward_weights(ctx);
        return status::success;
    }

private:
    void execute_backward_weights(const exec_ctx_t &ctx) const;
    bool do_parallel_reduction() const { return false; }
    const pd_t *pd() const { return (const pd_t *)primitive_t::pd(); }

    jit_uni_dw_conv_bwd_weights_kernel_f32<isa> *kernel_;
    cpu_accumulator_1d_t<data_type::f32> *acc_ker_;
};

using jit_avx512_common_dw_convolution_bwd_weights_t =
    _jit_uni_dw_convolution_bwd_weights_t<avx512_common>;
using jit_avx2_dw_convolution_bwd_weights_t =
    _jit_uni_dw_convolution_bwd_weights_t<avx2>;
using jit_sse42_dw_convolution_bwd_weights_t =
    _jit_uni_dw_convolution_bwd_weights_t<sse42>;

}
}
}

#endif
