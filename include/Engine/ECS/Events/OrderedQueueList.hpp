#ifndef ECS_ORDEREDQUEUELIST_HPP
#define ECS_ORDEREDQUEUELIST_HPP

namespace ECS {

namespace EVENT {

// デフォルトの比較対象オブジェクト
struct OrderedQueueListCompare {
	// 比較は内部のイベント（または識別子）を対象とする
	template <typename U>
	bool operator()(const U& a, const U& b) const {
		return a.event < b.event;
	}
};

template <typename T, typename Compare = OrderedQueueListCompare>
class OrderedQueueList {
public:
	// 内部コンテナには、T を直接扱えないため unique_ptr<T> で保持
	using container_type = std::vector<std::unique_ptr<T>>;
	using value_type = T;

	OrderedQueueList() = default;
	~OrderedQueueList() = default;
	OrderedQueueList(const OrderedQueueList&) = delete;
	OrderedQueueList& operator=(const OrderedQueueList&) = delete;
	OrderedQueueList(OrderedQueueList&&) noexcept = default;
	OrderedQueueList& operator=(OrderedQueueList&&) noexcept = default;

	class iterator {
		using underlying_iterator = typename container_type::iterator;
		underlying_iterator it;

	public:
		explicit iterator(underlying_iterator it_) : it(it_) { }
		T* operator->() const { return it->get(); }
		T& operator*() const { return *(it->get()); }
		iterator& operator++() { ++it; return *this; }
		bool operator!=(const iterator& other) const { return it != other.it; }
		bool operator==(const iterator& other) const { return it == other.it; }
		// 内部イテレータを取得（内部実装用）
		underlying_iterator get_underlying() const { return it; }
	};

	// splice（list::splice に似せた機能）
	// 　this->splice(pos, source, from) : source の from 位置の要素を、this の pos 位へムーブし、source から削除します。
	void splice(iterator pos, OrderedQueueList& source, iterator from) {
		// ソース側のインデックスを算出
		size_t index = std::distance(source.vec.begin(), from.get_underlying());
		// ムーブして取り出し
		std::unique_ptr<T> movedElem = std::move(source.vec[index]);
		source.vec.erase(source.vec.begin() + index);
		// 挿入先位置のインデックスを算出
		size_t posIndex = std::distance(this->vec.begin(), pos.get_underlying());
		this->vec.insert(this->vec.begin() + posIndex, std::move(movedElem));
	}

	// push: 既存のオブジェクト（unique_ptr<T>）をソートされた位置に挿入
	void push_back(std::unique_ptr<T> item) {
		Compare comp;
		auto pos = std::lower_bound(
			vec.begin(), vec.end(), item,
			[&comp](const std::unique_ptr<T>& a, const std::unique_ptr<T>& b) {
				if (a->empty()) return !b->empty();
				else if (b->empty()) return false;
				return comp(a->get(), b->get());
			}
		);
		vec.insert(pos, std::move(item));
	}

	// emplace_back: 新規要素を生成して末尾に追加（enqueue 側で空時に呼び出されます）
	void emplace_back() {
		vec.push_back(std::make_unique<T>());
	}

	// front: 最先頭のオブジェクトへの参照を返す
	T& front() {
		assert(!vec.empty());
		return *(vec.front());
	}
	const T& front() const {
		assert(!vec.empty());
		return *(vec.front());
	}

	// pop: 先頭要素を削除
	void erase(T& item) {
		assert(!vec.empty());
		vec.erase(item);
	}

	void pop() {
		assert(!vec.empty());
		vec.erase(vec.front());
	}

	bool empty() const { return vec.empty(); }
	std::size_t size() const { return vec.size(); }

	void recycleEmptyItems() {
		vec.erase(
			std::remove_if(vec.begin(), vec.end(),
				[](const std::unique_ptr<T>& item) { return item->emtpty(); }),
			vec.end());
	}

	// begin/end の提供（カスタムイテレータを返す）
	auto begin() { return vec.begin(); }
	auto end() { return vec.end(); }

	container_type extractVec()&& {
		return std::move(vec);
	}

private:
	container_type vec;
};

}//namespace EVENT
}//namespace ECS
#endif