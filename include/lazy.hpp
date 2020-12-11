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

#include <any>
#include <list>
#include <memory>
#include <tuple>
#include <type_traits>

namespace lazy {
   // shared -> lazy_base
   //                 -> data_lazy_base -> typed_data_lazy_base
   //                 -> function_lazy_base -> typed_data_lazy_base
   // 一个需要有一个init函数
   // 因为shared_from_this cannot be called from ctor
   class lazy_base : public virtual std::enable_shared_from_this<lazy_base> {
      /**
       * 重置当前节点
       */
      virtual void release(){};

   public:
      std::list<std::weak_ptr<lazy_base>> downstream;

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
   };

   class data_lazy_base : public virtual lazy_base {
   public:
      /**
       * 从一个any中load出数据
       */
      virtual void load(std::any) = 0;
      /**
       * dump出一个any数据
       */
      virtual std::any dump() = 0;
   };

   class function_lazy_base : public virtual lazy_base {};

   template<typename Type>
   class typed_data_lazy_base : public virtual data_lazy_base {
      // data lazy的release应reset掉value
      void release() override {
         value.reset();
      }

   protected:
      std::shared_ptr<Type> value;

   public:
      void load(std::any v) override {
         value = std::any_cast<std::shared_ptr<Type>>(v);
      }

      std::any dump() override {
         return std::any(value);
      }

      void set(Type&& v) {
         unset();
         value.reset(new Type(std::move(v)));
      }
      void set(const Type& v) {
         unset();
         value.reset(new Type(v));
      }
   };

   template<typename Function>
   class typed_function_lazy_base : public virtual function_lazy_base {
   protected:
      Function function;

   public:
      using Type = std::invoke_result_t<Function>;

      typed_function_lazy_base(Function&& f) : function(std::move(f)) {}
   };

   template<typename Type>
   struct root : typed_data_lazy_base<Type> {
      using typed_data_lazy_base<Type>::value;
      using typed_data_lazy_base<Type>::set;

      const Type& get() {
         return *value;
      }
      const Type& operator*() {
         return get();
      }

      root<Type>& operator=(const Type& v) {
         set(v);
         return *this;
      }
      root<Type>& operator=(Type&& v) {
         set(std::move(v));
         return *this;
      }
   };

   template<typename Function>
   struct path : typed_function_lazy_base<Function> {
      using typename typed_function_lazy_base<Function>::Type;
      using typed_function_lazy_base<Function>::function;
      using typed_function_lazy_base<Function>::typed_function_lazy_base;

      Type get() {
         return function();
      }
      const Type& operator*() {
         return get();
      }
   };

   template<typename Function>
   struct node : typed_function_lazy_base<Function>, typed_data_lazy_base<typename typed_function_lazy_base<Function>::Type> {
      using typename typed_function_lazy_base<Function>::Type;
      using typed_data_lazy_base<Type>::value;
      using typed_data_lazy_base<Type>::set;
      using typed_function_lazy_base<Function>::function;
      using typed_function_lazy_base<Function>::typed_function_lazy_base;

      const Type& get() {
         if (!bool(value)) {
            set(function());
         }
         return *value;
      }
      const Type& operator*() {
         return get();
      }
   };
   // graph

   using Snapshot = std::list<std::tuple<std::weak_ptr<data_lazy_base>, std::any>>;
   /**
    * Graph用于restore各个node中的数据
    */
   struct Graph {
      // std::any will store std::shared_ptr<T> for any T
      std::list<std::weak_ptr<data_lazy_base>> nodes;

      Snapshot dump() {
         auto result = Snapshot();
         for (auto iter = nodes.begin(); iter != nodes.end();) {
            if (auto ptr = iter->lock(); ptr) {
               result.push_back(std::make_tuple(*iter, ptr->dump()));
               ++iter;
            } else {
               iter = nodes.erase(iter);
            }
         }
         return result;
      }
      void load(Snapshot& snapshot) {
         for (auto iter = snapshot.begin(); iter != snapshot.end();) {
            auto& [weak, value] = *iter;
            if (auto ptr = weak.lock(); ptr) {
               ptr->load(value);
               ++iter;
            } else {
               iter = snapshot.erase(iter);
            }
         }
      }

      template<typename T>
      void add(T value) {
         nodes.push_back(std::dynamic_pointer_cast<data_lazy_base>(value));
      }
   };

   inline Graph default_graph = Graph();
   inline Graph* active_graph = &default_graph;
   inline Graph& get_graph() {
      return *active_graph;
   }
   inline void set_graph(Graph& graph) {
      active_graph = &graph;
   }

   // helper function

   template<typename Function, typename... Args>
   auto function_wrapper(Function&& function, Args&... args) {
      return [=] { return function(args->get()...); };
   }

   template<typename Type>
   auto Root(Type&& v) {
      using RealType = std::remove_cv_t<std::remove_reference_t<Type>>;
      auto result = std::make_shared<root<RealType>>();
      result->set(std::forward<Type>(v));
      active_graph->add(result);
      return result;
   }

   template<typename Function, typename... Args>
   auto Node(Function&& function, Args&... args) {
      auto f = function_wrapper(function, args...);
      auto result = std::make_shared<node<decltype(f)>>(std::move(f));
      (args->downstream.push_back(result->shared_from_this()), ...);
      active_graph->add(result);
      return result;
   }

   template<typename Function, typename... Args>
   auto Path(Function&& function, Args&... args) {
      auto f = function_wrapper(function, args...);
      auto result = std::make_shared<path<decltype(f)>>(std::move(f));
      (args->downstream.push_back(result->shared_from_this()), ...);
      return result;
   }
} // namespace lazy

#endif
