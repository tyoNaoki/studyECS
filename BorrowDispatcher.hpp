#ifndef ECS_BORROWDISPATCHER_HPP
#define ECS_BORROWDISPATCHER_HPP

#include "CallbackList.hpp"
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <functional>
#include <cstdint>


namespace ECS {

	namespace EVENT {

		template<class,bool>
		struct Borrow;

		template<class T, bool Mut = false>
		struct Borrow {               // Mut = false なら const 借用
			using value_type = T;
			using pointer = std::conditional_t<Mut, T*, const T*>;
			pointer ptr{nullptr};
			//const void* dbg_owner_ =/* 取り出し元の this など */;

			~Borrow() {
				// 所有者 still alive? などをチェックして abort
			}

			// 値チェック
			explicit operator bool() const noexcept { return ptr != nullptr; }

			// アロー演算子
			auto operator->() const noexcept { return ptr; }

			// 間接演算子
			auto& operator*()  const noexcept { return *ptr; }

		};

		template<class T> using BorrowConst = Borrow<T, false>;
		template<class T> using BorrowMut = Borrow<T, true>;
		template<class T> constexpr BorrowConst<T> make_borrow(const T* p) { return { p }; }
		template<class T> constexpr BorrowMut <T> make_borrow(T* p) { return { p }; }

		template<class> struct is_borrow : std::false_type {};
		template<class T,bool M> struct is_borrow<Borrow<T, M>> : std::true_type {};

		class BorrowDispatcher {
			using HashID = std::size_t;
			using RawArg = void*;
			using Wrapper = std::function<void(RawArg)>;
		
		public:
			template<class BorrowT>
			void appendTestListener(HashID id,std::function<void(BorrowT)>cd)
			{
				static_assert(is_borrow<BorrowT>::value,
					"Listener must take Borrow<T*> or Borrow<const T*>");
				Wrapper w = [cd](RawArg raw) {
					cd(convertArg<BorrowT>(raw));     // ★ 型安全に１点変換
				};

				std::unique_lock li(mtx_);
				listeners_[id].emplace_back(std::move(w));
			}

			template<class ObjT>
			void publish(HashID id, ObjT* obj)                    // 非 const*
			{
				enqueue(id, static_cast<RawArg>(obj));
			}

			template<class ObjT>
			void publish(HashID id, const ObjT* obj)              // const*
			{
				enqueue(id, const_cast<ObjT*>(obj));
			}

			/*-- フレーム末などで一括ディスパッチ -------------------------------*/
			void dispatch()
			{
				std::vector<std::pair<HashID, RawArg>> work;
				{
					std::unique_lock lk(mtx_);
					work.swap(queue_);
				}
				for (auto& [id, raw] : work) {
					auto it = listeners_.find(id);
					if (it == listeners_.end()) continue;
					for (auto& fn : it->second) fn(raw);
				}
			}


		private:
			/* -- RawArg → Borrow<T*> 変換 -------------------------------------- */
			template<class Want>
			static Want convertArg(RawArg raw) {
				using Elem = typename Want::value_type;
				using Ptr = typename Want::pointer;
				return { static_cast<Ptr>(
						   static_cast<Elem*>(raw)) };
			}

			void enqueue(HashID id, RawArg raw) {
				std::unique_lock lk(mtx_);
				queue_.emplace_back(id, raw);
			}

			std::unordered_map<HashID,std::vector<Wrapper>>listeners_;
			std::vector<std::pair<HashID,RawArg>>queue_;
			std::shared_mutex mtx_;
		};

	}//namespace EVENT
}//namespace ECS
#endif
