/**
 * \file lazy.hpp
 *
 * Copyright (C) 2020 Hao Zhang<zh970205@mail.ustc.edu.cn>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#ifndef LAZY_HPP
#define LAZY_HPP

#ifndef __cplusplus
#error only work for c++
#endif

#ifdef _MSVC_LANG
#if _MSVC_LANG < 201703L
#error require c++17 or later
#endif
#else
#if __cplusplus < 201703L
#error require c++17 or later
#endif
#endif

#include <list>
#include <memory>
#include <optional>
#include <type_traits>

namespace lazy {

   class _Base : public std::enable_shared_from_this<_Base> {
      /**
       * 重置当前节点
       */
      virtual void release() = 0;

   public:
      /**
       * 重置当前节点和下游
       */
      void unset(bool unset_itself = true) {
         if (unset_itself) {
            release();
         }
         for (auto iter = downstream.begin(); iter != downstream.end();) {
            if (auto ptr = iter->lock(); ptr) {
               ptr->unset();
               ++iter;
            } else {
               iter = downstream.erase(iter);
            }
         }
      }

      std::list<std::weak_ptr<_Base>> downstream;
   };

   template<typename Type>
   class _Root : public _Base {
      std::optional<Type> value;

      void release() override {
         value.reset();
      }

   public:
      _Root(Type&& v) {
         set(std::move(v));
      }

      const Type& get() {
         return *value;
      }

      void set(const Type& v) {
         unset();
         value = v;
      }
      void set(Type&& v) {
         unset();
         value = std::move(v);
      }

      // 下面是一些非正交函数

      _Root<Type>& operator=(const Type& v) {
         set(v);
         return *this;
      }
      _Root<Type>& operator=(Type&& v) {
         set(std::move(v));
         return *this;
      }

      const Type& operator*() {
         return get();
      }
   };
   template<typename Type>
   auto Root(const Type& v) {
      return std::make_shared<_Root<Type>>(v);
   }
   template<typename Type>
   auto Root(Type&& v) {
      return std::make_shared<_Root<Type>>(std::move(v));
   }

   template<typename CreateFunction, typename... Args>
   auto function_wrapper(CreateFunction&& function, Args&... args) {
      return [=]() { return function(args->get()...); };
   }

   template<typename Function>
   class _Node : public _Base {
      using Type = std::invoke_result_t<Function>;
      std::optional<Type> value;
      // Function is ()=>Type
      Function function;

      void release() override {
         value.reset();
      }

   public:
      const Type& get() {
         if (!value.has_value()) {
            value = function();
         }
         return *value;
      }

      _Node(Function&& f) : function(std::move(f)) {}

      // 下面是一些非正交函数
      const Type& operator*() {
         return get();
      }
   };

   template<typename CreateFunction, typename... Args>
   auto Node(CreateFunction&& function, Args&... args) {
      auto f = function_wrapper(function, args...);
      auto result = std::make_shared<_Node<decltype(f)>>(std::move(f));
      (args->downstream.push_back(result->shared_from_this()), ...);
      return result;
   }

   template<typename Function>
   class _Path : public _Base {
      using Type = std::invoke_result_t<Function>;
      // Function is ()=>Type
      Function function;

      void release() override {}

   public:
      Type get() {
         return function();
      }

      _Path(Function&& f) : function(std::move(f)) {}

      // 下面是一些非正交函数
      const Type& operator*() {
         return get();
      }
   };
   template<typename CreateFunction, typename... Args>
   auto Path(CreateFunction&& function, Args&... args) {
      auto f = function_wrapper(function, args...);
      auto result = std::make_shared<_Path<decltype(f)>>(std::move(f));
      (args->downstream.push_back(result->shared_from_this()), ...);
      return result;
   }
} // namespace lazy

#endif
