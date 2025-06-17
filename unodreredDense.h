#pragma once
#include "HashFunctions.h"
#include <iostream>
#include <vector>

#if defined(_MSC_VER)
// MSVC向けのアラインメント制御
#pragma pack(push, 1)
#endif

struct AlignedBucket {
	uint8_t a;
	uint32_t b;
};

#if defined(_MSC_VER)
// 元のアラインメント設定を復元
#pragma pack(pop)
#endif

namespace ecs_map{
	template <typename T, typename = void>
	struct has_reserve : std::false_type {};  // デフォルトでは `false`

	template <typename T>
	struct has_reserve<T, std::void_t<decltype(std::declval<T>().reserve(std::declval<size_t>()))>> : std::true_type {}; // `reserve()` がある場合は `true`

	namespace bucket_type {

		struct standard {
			static constexpr uint32_t dist_inc = 1U << 8U;             // skip 1 byte fingerprint
			static constexpr uint32_t fingerprint_mask = dist_inc - 1; // mask for 1 byte of fingerprint

			uint32_t m_dist_and_fingerprint; // upper 3 byte: distance to original bucket. lower byte: fingerprint from hash
			uint32_t m_value_idx;            // index into the m_values vector.
		};

	} // namespace bucket_type

	// Very much like std::deque, but faster for indexing (in most cases). As of now this doesn't implement the full std::vector
// API, but merely what's necessary to work as an underlying container for ankerl::unordered_dense::{map, set}.
// It allocates blocks of equal size and puts them into the m_blocks vector. That means it can grow simply by adding a new
// block to the back of m_blocks, and doesn't double its size like an std::vector. The disadvantage is that memory is not
// linear and thus there is one more indirection necessary for indexing.
	template <typename T, typename Allocator = std::allocator<T>, size_t MaxSegmentSizeBytes = 4096>
	class segmented_vector {
		template <bool IsConst>
		class iter_t;

	public:
		using allocator_type = Allocator;
		using pointer = typename std::allocator_traits<allocator_type>::pointer;
		using const_pointer = typename std::allocator_traits<allocator_type>::const_pointer;
		using difference_type = typename std::allocator_traits<allocator_type>::difference_type;
		using value_type = T;
		using size_type = std::size_t;
		using reference = T&;
		using const_reference = T const&;
		using iterator = iter_t<false>;
		using const_iterator = iter_t<true>;

	private:
		using vec_alloc = typename std::allocator_traits<Allocator>::template rebind_alloc<pointer>;
		std::vector<pointer, vec_alloc> m_blocks{};
		size_t m_size{};

		// Calculates the maximum number for x in  (s << x) <= max_val
		static constexpr auto num_bits_closest(size_t max_val, size_t s) -> size_t {
			auto f = size_t{ 0 };
			while (s << (f + 1) <= max_val) {
				++f;
			}
			return f;
		}

		using self_t = segmented_vector<T, Allocator, MaxSegmentSizeBytes>;
		static constexpr auto num_bits = num_bits_closest(MaxSegmentSizeBytes, sizeof(T));
		static constexpr auto num_elements_in_block = 1U << num_bits;
		static constexpr auto mask = num_elements_in_block - 1U;

		/**
		 * Iterator class doubles as const_iterator and iterator
		 */
		template <bool IsConst>
		class iter_t {
			using ptr_t = typename std::conditional_t<IsConst, segmented_vector::const_pointer const*, segmented_vector::pointer*>;
			ptr_t m_data{};
			size_t m_idx{};

			template <bool B>
			friend class iter_t;

		public:
			using difference_type = segmented_vector::difference_type;
			using value_type = T;
			using reference = typename std::conditional_t<IsConst, value_type const&, value_type&>;
			using pointer = typename std::conditional_t<IsConst, segmented_vector::const_pointer, segmented_vector::pointer>;
			using iterator_category = std::forward_iterator_tag;

			iter_t() noexcept = default;

			template <bool OtherIsConst, typename = typename std::enable_if<IsConst && !OtherIsConst>::type>
			// NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
			constexpr iter_t(iter_t<OtherIsConst> const& other) noexcept
				: m_data(other.m_data)
				, m_idx(other.m_idx) {}

			constexpr iter_t(ptr_t data, size_t idx) noexcept
				: m_data(data)
				, m_idx(idx) {}

			template <bool OtherIsConst, typename = typename std::enable_if<IsConst && !OtherIsConst>::type>
			constexpr auto operator=(iter_t<OtherIsConst> const& other) noexcept -> iter_t& {
				m_data = other.m_data;
				m_idx = other.m_idx;
				return *this;
			}

			constexpr auto operator++() noexcept -> iter_t& {
				++m_idx;
				return *this;
			}

			constexpr auto operator+(difference_type diff) noexcept -> iter_t {
				return { m_data, static_cast<size_t>(static_cast<difference_type>(m_idx) + diff) };
			}

			template <bool OtherIsConst>
			constexpr auto operator-(iter_t<OtherIsConst> const& other) noexcept -> difference_type {
				return static_cast<difference_type>(m_idx) - static_cast<difference_type>(other.m_idx);
			}

			constexpr auto operator*() const noexcept -> reference {
				return m_data[m_idx >> num_bits][m_idx & mask];
			}

			constexpr auto operator->() const noexcept -> pointer {
				return &m_data[m_idx >> num_bits][m_idx & mask];
			}

			template <bool O>
			constexpr auto operator==(iter_t<O> const& o) const noexcept -> bool {
				return m_idx == o.m_idx;
			}

			template <bool O>
			constexpr auto operator!=(iter_t<O> const& o) const noexcept -> bool {
				return !(*this == o);
			}
		};

		// slow path: need to allocate a new segment every once in a while
		void increase_capacity() {
			auto ba = Allocator(m_blocks.get_allocator());
			pointer block = std::allocator_traits<Allocator>::allocate(ba, num_elements_in_block);
			m_blocks.push_back(block);
		}

		// Moves everything from other
		void append_everything_from(segmented_vector&& other) {
			reserve(size() + other.size());
			for (auto&& o : other) {
				emplace_back(std::move(o));
			}
		}

		// Copies everything from other
		void append_everything_from(segmented_vector const& other) {
			reserve(size() + other.size());
			for (auto const& o : other) {
				emplace_back(o);
			}
		}

		void dealloc() {
			auto ba = Allocator(m_blocks.get_allocator());
			for (auto ptr : m_blocks) {
				std::allocator_traits<Allocator>::deallocate(ba, ptr, num_elements_in_block);
			}
		}

		[[nodiscard]] static constexpr auto calc_num_blocks_for_capacity(size_t capacity) {
			return (capacity + num_elements_in_block - 1U) / num_elements_in_block;
		}

public:
	segmented_vector() = default;

	// NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
	segmented_vector(Allocator alloc)
		: m_blocks(vec_alloc(alloc)) {}

	segmented_vector(segmented_vector && other, Allocator alloc)
		: segmented_vector(alloc) {
		*this = std::move(other);
	}

	segmented_vector(segmented_vector const& other, Allocator alloc)
		: m_blocks(vec_alloc(alloc)) {
		append_everything_from(other);
	}

	segmented_vector(segmented_vector && other) noexcept
		: segmented_vector(std::move(other), get_allocator()) {}

	segmented_vector(segmented_vector const& other) {
		append_everything_from(other);
	}

	auto operator=(segmented_vector const& other) -> segmented_vector& {
		if (this == &other) {
			return *this;
		}
		clear();
		append_everything_from(other);
		return *this;
	}

	auto operator=(segmented_vector && other) noexcept -> segmented_vector& {
		clear();
		dealloc();
		if (other.get_allocator() == get_allocator()) {
			m_blocks = std::move(other.m_blocks);
			m_size = std::exchange(other.m_size, {});
		}
		else {
			// make sure to construct with other's allocator!
			m_blocks = std::vector<pointer, vec_alloc>(vec_alloc(other.get_allocator()));
			append_everything_from(std::move(other));
		}
		return *this;
	}

	~segmented_vector() {
		clear();
		dealloc();
	}

	[[nodiscard]] constexpr auto size() const -> size_t {
		return m_size;
	}

	[[nodiscard]] constexpr auto capacity() const -> size_t {
		return m_blocks.size() * num_elements_in_block;
	}

	// Indexing is highly performance critical
	[[nodiscard]] constexpr auto operator[](size_t i) const noexcept -> T const& {
		return m_blocks[i >> num_bits][i & mask];
	}

	[[nodiscard]] constexpr auto operator[](size_t i) noexcept -> T& {
		return m_blocks[i >> num_bits][i & mask];
	}

	[[nodiscard]] constexpr auto begin() -> iterator {
		return { m_blocks.data(), 0U };
	}
	[[nodiscard]] constexpr auto begin() const -> const_iterator {
		return { m_blocks.data(), 0U };
	}
	[[nodiscard]] constexpr auto cbegin() const -> const_iterator {
		return { m_blocks.data(), 0U };
	}

	[[nodiscard]] constexpr auto end() -> iterator {
		return { m_blocks.data(), m_size };
	}
	[[nodiscard]] constexpr auto end() const -> const_iterator {
		return { m_blocks.data(), m_size };
	}
	[[nodiscard]] constexpr auto cend() const -> const_iterator {
		return { m_blocks.data(), m_size };
	}

	[[nodiscard]] constexpr auto back() -> reference {
		return operator[](m_size - 1);
	}
	[[nodiscard]] constexpr auto back() const -> const_reference {
		return operator[](m_size - 1);
	}

	void pop_back() {
		back().~T();
		--m_size;
	}

	[[nodiscard]] auto empty() const {
		return 0 == m_size;
	}

	void reserve(size_t new_capacity) {
		m_blocks.reserve(calc_num_blocks_for_capacity(new_capacity));
		while (new_capacity > capacity()) {
			increase_capacity();
		}
	}

	[[nodiscard]] auto get_allocator() const -> allocator_type {
		return allocator_type{ m_blocks.get_allocator() };
	}

	template <class... Args>
	auto emplace_back(Args&&... args) -> reference {
		if (m_size == capacity()) {
			increase_capacity();
		}
		auto* ptr = static_cast<void*>(&operator[](m_size));
		auto& ref = *new (ptr) T(std::forward<Args>(args)...);
		++m_size;
		return ref;
	}

	void clear() {
		if constexpr (!std::is_trivially_destructible_v<T>) {
			for (size_t i = 0, s = size(); i < s; ++i) {
				operator[](i).~T();
			}
		}
		m_size = 0;
	}

	void shrink_to_fit() {
		auto ba = Allocator(m_blocks.get_allocator());
		auto num_blocks_required = calc_num_blocks_for_capacity(m_size);
		while (m_blocks.size() > num_blocks_required) {
			std::allocator_traits<Allocator>::deallocate(ba, m_blocks.back(), num_elements_in_block);
			m_blocks.pop_back();
		}
		m_blocks.shrink_to_fit();
		}
	};
	
	/*
	struct Bucket {
		uint32_t dist_and_fingerprint;
		uint32_t value_idx;
		
	};
	*/
	template<typename Key,typename Value>
	class unordered_dense_map {
		using Bucket = bucket_type::standard;
		using dist_and_fingerprint_type = decltype(Bucket::m_dist_and_fingerprint);
		using value_idx_type = decltype(Bucket::m_value_idx);
		using value_container_type = segmented_vector<std::pair<Key, Value>>;
		using bucket_alloc = typename std::allocator_traits<typename value_container_type::allocator_type>::template rebind_alloc<Bucket>;
		using bucket_pointer = typename std::allocator_traits<bucket_alloc>::pointer;
		using bucket_alloc_traits = std::allocator_traits<bucket_alloc>;

		static constexpr uint8_t initial_shifts = 64 - 3; // 2^(64-m_shift) number of buckets
		static constexpr float default_max_load_factor = 0.8F;
	public:
		using iterator = typename value_container_type::iterator;
		using difference_type = typename value_container_type::difference_type;
		using allocator_type = typename value_container_type::allocator_type;

		explicit unordered_dense_map(size_t bucket_count,allocator_type const& alloc_or_container = allocator_type()){
			reserve(bucket_count);
		};

		void reserve(size_t cap);

		const Value* find(const Key&key)const;

		auto insert(Key key, Value value);

		auto emplace(Key&&key,Value&&value);

		auto begin() noexcept -> iterator {
			return m_values.begin();
		}

		auto end() noexcept -> iterator{
			return m_values.end();
		}

	private:
		bucket_pointer  m_buckets{};
		value_container_type m_values{};
		size_t capacity;

		uint8_t m_shifts = initial_shifts;
		size_t m_num_buckets = 0;
		size_t m_max_bucket_capacity = 0;

		float m_max_load_factor = default_max_load_factor;

		template <typename hashType>
		size_t hashToPos(const hashType hash){
			return hash % capacity;
		}

		auto do_place_element(dist_and_fingerprint_type dist_and_fingerprint, size_t position, Key&& key, Value&&value);

		void place_and_shift_up(Bucket bucket, value_idx_type place);

		[[nodiscard]] auto next_while_less(Key const& key) const -> Bucket {
			auto hash = CustomHash(key);
			auto dist_and_fingerprint = dist_and_fingerprint_from_hash(hash);
			auto bucket_idx = bucket_idx_from_hash(hash);

			while (dist_and_fingerprint < at(m_buckets, bucket_idx).m_dist_and_fingerprint) {
				dist_and_fingerprint = dist_inc(dist_and_fingerprint);
				bucket_idx = next(bucket_idx);
			}
			return { dist_and_fingerprint, bucket_idx };
		}

		[[nodiscard]] constexpr auto calc_shifts_for_size(size_t s) -> uint8_t {
			auto shifts = initial_shifts;
			while (shifts > 0 && static_cast<size_t>(static_cast<float>(calc_num_buckets(shifts)) * m_max_load_factor) < s) {
				--shifts;
			}
			return shifts;
		}

		constexpr auto dist_and_fingerprint_from_hash(uint64_t hash) const -> dist_and_fingerprint_type {
			return Bucket::dist_inc | (static_cast<dist_and_fingerprint_type>(hash) & Bucket::fingerprint_mask);
		}

		constexpr auto bucket_idx_from_hash(uint64_t hash) const -> value_idx_type {
			return static_cast<value_idx_type>(hash >> m_shifts);
		}

		constexpr auto dist_inc(dist_and_fingerprint_type x)const -> dist_and_fingerprint_type {
			return static_cast<dist_and_fingerprint_type>(x + Bucket::dist_inc);
		}

		constexpr auto at(bucket_pointer bucket_ptr, size_t offset)const -> Bucket& {
			return *(bucket_ptr + static_cast<typename std::allocator_traits<bucket_alloc>::difference_type>(offset));
		}

		constexpr auto calc_num_buckets(uint8_t shifts) -> size_t {
			return (std::min)(max_size(), size_t{ 1 } << (64U - shifts));
		}

		constexpr auto max_size() noexcept -> size_t {
			if constexpr ((std::numeric_limits<value_idx_type>::max)() == (std::numeric_limits<size_t>::max)()) {
				return size_t{ 1 } << (sizeof(value_idx_type) * 8 - 1);
			}
			else {
				return size_t{ 1 } << (sizeof(value_idx_type) * 8);
			}
		}

		constexpr auto get_key(std::pair<Key,Value> const& vt) -> Key const& {
			return vt.first;
		}

		constexpr auto next(uint32_t pos)const -> uint32_t{
			return (pos + 1U == m_num_buckets) ? 0 : static_cast<value_idx_type>(pos + 1U);
		}

		void allocate_buckets_from_shift();

		void deallocate_buckets();

		void clear_and_fill_buckets_from_values();

		void clear_buckets();
	};

	template<typename Key, typename Value>
	inline void unordered_dense_map<Key, Value>::reserve(size_t cap)
	{
		capacity = (std::min)(cap, max_size());
		if constexpr (has_reserve<value_container_type>::value) {
			// std::deque doesn't have reserve(). Make sure we only call when available
			m_values.reserve(cap);
		}
		auto shifts = calc_shifts_for_size(cap);
		if (0 == m_num_buckets || shifts < m_shifts) {
			m_shifts = shifts;
			deallocate_buckets();
			allocate_buckets_from_shift();
			clear_and_fill_buckets_from_values();
		}
	}

	template<typename Key,typename Value>
	inline const Value* unordered_dense_map<Key,Value>::find(const Key&key) const{
		{
			if (capacity <= 0) {
				return nullptr;
			}

			size_t hash = CustomHash(key);
			size_t pos = bucket_idx_from_hash(hash);
			size_t dist_and_fingerprint = dist_and_fingerprint_from_hash(hash);
			while (true) {

				auto* bucket = &at(m_buckets, pos);
				// フィンガープリントで高速フィルタリング
				if (dist_and_fingerprint == bucket->m_dist_and_fingerprint) {
					
					// 実際のキーを比較して一致するか確認
					if (m_values[bucket->m_value_idx].first == key) {
						return &m_values[bucket->m_value_idx].second;
					}
				}else if(dist_and_fingerprint > bucket->m_dist_and_fingerprint){
					return nullptr;
				}

				// プローブ距離を考慮して次のバケットへ移動
				pos = next(pos);
				dist_and_fingerprint = dist_inc(dist_and_fingerprint);

				/*
				// もし探索範囲を超えたら検索失敗
				if (dist > MAX_PROBE_DISTANCE) {
					return end();
				}
				*/
			}

			return nullptr;
		}
	}

	template<typename Key, typename Value>
	inline auto unordered_dense_map<Key, Value>::insert(Key key, Value value)
	{
		return emplace(std::move(key),std::move(value)); 
	}

	template<typename Key, typename Value>
	inline auto unordered_dense_map<Key, Value>::emplace(Key&&key,Value&&value)
	{
		if(m_values.size()>capacity){
			return std::pair<iterator, bool>{ end(), false };
		
		}
		auto hash = CustomHash(key);
		auto dist_and_fingerprint = dist_and_fingerprint_from_hash(hash);
		auto pos = bucket_idx_from_hash(hash);
		while (true) {
			auto* bucket = &at(m_buckets, pos);
			if (dist_and_fingerprint == bucket->m_dist_and_fingerprint) {
				if (key == m_values[bucket->m_value_idx].first) {
					return std::pair<iterator, bool>{ begin() + static_cast<difference_type>(at(m_buckets, pos).m_value_idx), false };
				}
			}
			else if (dist_and_fingerprint > bucket->m_dist_and_fingerprint) {
				return do_place_element(dist_and_fingerprint, pos, std::forward<Key>(key), std::forward<Value>(value));
			}

			dist_and_fingerprint = dist_inc(dist_and_fingerprint);
			pos = next(pos);
		}

	}

	template<typename Key,typename Value>
	inline auto unordered_dense_map<Key, Value>::do_place_element(dist_and_fingerprint_type dist_and_fingerprint, size_t position, Key&& key,Value&&value)
	{

		// emplace the new value. If that throws an exception, no harm done; index is still in a valid state
		m_values.emplace_back(std::piecewise_construct,
			std::forward_as_tuple(key),
			std::forward_as_tuple(value));

		// place element and shift up until we find an empty spot
		auto value_idx = static_cast<value_idx_type>(m_values.size() - 1);
		place_and_shift_up({ dist_and_fingerprint, value_idx }, position);
		return std::pair<iterator, bool>{ begin() + static_cast<difference_type>(value_idx), true };
	}

	template<typename Key, typename Value>
	inline void unordered_dense_map<Key, Value>::place_and_shift_up(Bucket bucket, value_idx_type place)
	{
		while (0 != at(m_buckets, place).m_dist_and_fingerprint) {
			bucket = std::exchange(at(m_buckets, place), bucket);
			bucket.m_dist_and_fingerprint = dist_inc(bucket.m_dist_and_fingerprint);
			place = (place + 1U == m_num_buckets)? 0: static_cast<size_t>(place + 1U);
		}

		at(m_buckets, place) = bucket;
	}

	template<typename Key, typename Value>
	inline void unordered_dense_map<Key, Value>::allocate_buckets_from_shift()
	{
		auto ba = bucket_alloc(m_values.get_allocator());
		m_num_buckets = calc_num_buckets(m_shifts);
		m_buckets = bucket_alloc_traits::allocate(ba, m_num_buckets);
		if (m_num_buckets == max_size()) {
			// reached the maximum, make sure we can use each bucket
			m_max_bucket_capacity = max_size();
		}
		else {
			m_max_bucket_capacity = static_cast<value_idx_type>(static_cast<float>(m_num_buckets) * m_max_load_factor);
		}
	}

	template<typename Key, typename Value>
	inline void unordered_dense_map<Key, Value>::deallocate_buckets()
	{
		auto ba = bucket_alloc(m_values.get_allocator());
		if (nullptr != m_buckets) {
			bucket_alloc_traits::deallocate(ba, m_buckets, m_num_buckets);
			m_buckets = nullptr;
		}
		m_num_buckets = 0;
		m_max_bucket_capacity = 0;
	}

	template<typename Key, typename Value>
	inline void unordered_dense_map<Key, Value>::clear_and_fill_buckets_from_values()
	{
		clear_buckets();
		for (value_idx_type value_idx = 0, end_idx = static_cast<value_idx_type>(m_values.size()); value_idx < end_idx;
			++value_idx) {
			auto const& key = get_key(m_values[value_idx]);
			auto [dist_and_fingerprint, bucket] = next_while_less(key);

			// we know for certain that key has not yet been inserted, so no need to check it.
			place_and_shift_up({ dist_and_fingerprint, value_idx }, bucket);
		}
	}

	template<typename Key, typename Value>
	inline void unordered_dense_map<Key, Value>::clear_buckets()
	{
		if (m_buckets != nullptr) {
			std::memset(&*m_buckets, 0, sizeof(Bucket) * m_num_buckets);
		}
	}

	template<typename Key,typename Value>
	class unorderd_dense_set {
		
	};
};
