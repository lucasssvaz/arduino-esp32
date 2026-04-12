/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
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
 */

#pragma once

#include <type_traits>
#include <utility>

/**
 * @brief Lightweight callback slot for internal BLE Impl structs.
 *
 * Replaces std::function<void(Args...)> in internal storage with a pair of:
 *   - typed function pointer (fn)
 *   - void* context (ctx)
 *
 * Flash savings over std::function:
 *   - No SBO machinery, no allocator, no virtual-dispatch table
 *   - Non-capturing lambdas (most common in sketches) are stored as plain
 *     function pointers with no heap allocation at all
 *   - Capturing lambdas / std::function are heap-allocated once
 *   - Per-instantiation code is ~50-100 bytes vs ~300-600 for std::function
 *
 * Public API still accepts std::function / lambda via set(); this class is
 * internal to the Impl structs only.
 */
template<typename... Args>
class BLECbSlot {
  using Fn = void (*)(void *, Args...);
  Fn _fn{};
  void *_ctx{};
  void (*_del)(void *){};

public:
  BLECbSlot() = default;

  ~BLECbSlot() {
    if (_del && _ctx) {
      _del(_ctx);
    }
  }

  BLECbSlot(const BLECbSlot &) = delete;
  BLECbSlot &operator=(const BLECbSlot &) = delete;

  BLECbSlot(BLECbSlot &&o) noexcept : _fn(o._fn), _ctx(o._ctx), _del(o._del) {
    o._fn = nullptr;
    o._ctx = nullptr;
    o._del = nullptr;
  }

  BLECbSlot &operator=(BLECbSlot &&o) noexcept {
    if (this != &o) {
      if (_del && _ctx) {
        _del(_ctx);
      }
      _fn = o._fn;
      _ctx = o._ctx;
      _del = o._del;
      o._fn = nullptr;
      o._ctx = nullptr;
      o._del = nullptr;
    }
    return *this;
  }

  /**
   * Register a callable. Accepts:
   *   - Non-capturing lambda / free function: stored as plain fn ptr, no heap alloc.
   *   - Capturing lambda / std::function: heap-allocated, freed on clear/reassign/destroy.
   *   - nullptr / empty std::function: clears the slot.
   */
  template<typename F>
  void set(F &&f) {
    if (_del && _ctx) {
      _del(_ctx);
    }
    _fn = nullptr;
    _ctx = nullptr;
    _del = nullptr;

    if (!static_cast<bool>(f)) {
      return;
    }

    using DF = std::decay_t<F>;

    if constexpr (std::is_convertible_v<DF, void (*)(Args...)>) {
      // Non-capturing lambda or raw function pointer: no heap allocation.
      void (*fptr)(Args...) = static_cast<void (*)(Args...)>(static_cast<DF>(std::forward<F>(f)));
      _ctx = reinterpret_cast<void *>(fptr);
      _fn = [](void *c, Args... a) {
        reinterpret_cast<void (*)(Args...)>(c)(std::forward<Args>(a)...);
      };
      // _del stays null — nothing to free
    } else {
      // Capturing lambda or std::function: heap-allocate.
      DF *stored = new DF(std::forward<F>(f));
      _ctx = stored;
      _fn = [](void *c, Args... a) {
        (*static_cast<DF *>(c))(std::forward<Args>(a)...);
      };
      _del = [](void *c) {
        delete static_cast<DF *>(c);
      };
    }
  }

  void clear() {
    if (_del && _ctx) {
      _del(_ctx);
    }
    _fn = nullptr;
    _ctx = nullptr;
    _del = nullptr;
  }

  void operator()(Args... args) const {
    if (_fn) {
      _fn(_ctx, std::forward<Args>(args)...);
    }
  }

  explicit operator bool() const {
    return _fn != nullptr;
  }
};
