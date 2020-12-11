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
#include <type_traits>

namespace lazy {
   class _Base : public std::enable_shared_from_this<_Base> {
      /**
       * 重置当前节点
       */
      virtual void release(){};

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

   class _DataBase : public _Base {
   public:
      virtual void load(std::any) = 0;
      virtual std::any dump() = 0;
   };

   using Snapshot = std::list<std::tuple<std::weak_ptr<_DataBase>, std::any>>;
   /**
    * Graph用于restore各个node中的数据
    */
   struct Graph {
      // std::any will store std::shared_ptr<T> for any T
      std::list<std::weak_ptr<_DataBase>> nodes;

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
         nodes.push_back(std::dynamic_pointer_cast<_DataBase>(value));
      }
   };

   inline Graph default_graph = Graph();
   inline Graph* current_graph = &default_graph;

   template<typename Type>
   class _DataNode : public _DataBase {
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
   };

   template<typename Derived>
   class _Common {
   public:
      auto operator*() {
         return static_cast<Derived*>(this)->Derived::get();
      }
   };

   template<typename Type>
   class _Root : public _DataNode<Type>, public _Common<_Root<Type>> {
   public:
      _Root(const Type& v) {
         set(v);
      }
      _Root(Type&& v) {
         set(std::move(v));
      }

      const Type& get() {
         return *(this->value);
      }

      void set(const Type& v) {
         this->unset();
         this->value = v;
      }
      void set(Type&& v) {
         this->unset();
         this->value.reset(new Type(std::move(v)));
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
   };
   template<typename Type>
   auto Root(const Type& v) {
      auto result = std::make_shared<_Root<Type>>(v);
      current_graph->add(result);
      return result;
   }
   template<typename Type>
   auto Root(Type&& v) {
      auto result = std::make_shared<_Root<Type>>(std::move(v));
      current_graph->add(result);
      return result;
   }

   template<typename CreateFunction, typename... Args>
   auto function_wrapper(CreateFunction&& function, Args&... args) {
      return [=]() { return function(args->get()...); };
   }

   template<typename Function>
   class _Node : public _DataNode<std::invoke_result_t<Function>>, public _Common<_Node<Function>> {
      using Type = std::invoke_result_t<Function>;

      // Function is ()=>Type
      Function function;

   public:
      const Type& get() {
         if (!bool(this->value)) {
            this->value.reset(new Type(function()));
         }
         return *(this->value);
      }

      _Node(Function&& f) : function(std::move(f)) {}
   };

   template<typename CreateFunction, typename... Args>
   auto Node(CreateFunction&& function, Args&... args) {
      auto f = function_wrapper(function, args...);
      auto result = std::make_shared<_Node<decltype(f)>>(std::move(f));
      (args->downstream.push_back(result->shared_from_this()), ...);
      current_graph->add(result);
      return result;
   }

   template<typename Function>
   class _Path : public _Base, public _Common<_Path<Function>> {
      using Type = std::invoke_result_t<Function>;
      // Function is ()=>Type
      Function function;

   public:
      Type get() {
         return function();
      }

      _Path(Function&& f) : function(std::move(f)) {}
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
