//
// detail/throw_error.hpp
// ~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2024 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_THROW_ERROR_HPP
#define ASIO_DETAIL_THROW_ERROR_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#pragma once
#endif// defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"
#include "asio/detail/throw_exception.hpp"
#include "asio/error_code.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
    namespace detail {

        ASIO_DECL void do_throw_error(
                const asio::error_code &err
                        ASIO_SOURCE_LOCATION_PARAM);

        ASIO_DECL void do_throw_error(
                const asio::error_code &err,
                const char *location
                        ASIO_SOURCE_LOCATION_PARAM);

        inline void throw_error(
                const asio::error_code &err
                        ASIO_SOURCE_LOCATION_DEFAULTED_PARAM) {
            if (err)
                do_throw_error(err ASIO_SOURCE_LOCATION_ARG);
        }

        inline void throw_error(
                const asio::error_code &err,
                const char *location
                        ASIO_SOURCE_LOCATION_DEFAULTED_PARAM) {
            if (err)
                do_throw_error(err, location ASIO_SOURCE_LOCATION_ARG);
        }

    }// namespace detail
}// namespace asio

#include "asio/detail/pop_options.hpp"

#if defined(ASIO_HEADER_ONLY)
#include "asio/detail/impl/throw_error.ipp"
#endif// defined(ASIO_HEADER_ONLY)

#endif// ASIO_DETAIL_THROW_ERROR_HPP
