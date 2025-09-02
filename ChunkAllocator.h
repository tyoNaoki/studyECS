#pragma once
#include <cstdint> 

namespace ECS::JobSystem{
    //参考URL
    // https://qiita.com/taqu/items/45ab4fb57e4079c3be94
    // Qiita記事[Chunk allocator]
    // 
    //Capactityは512まで指定可能
    template <typename T, size_t Capacity, size_t ChunkCount, size_t Align = 16>
    class ChunkAllocator {
        using u16 = std::uint16_t;

    public:
        struct Chunk {
            u16 next_{};
            std::atomic<u16> start{ 0 };
            std::atomic<u16> count{ 0 };
            std::array<T, Capacity> tasks{};
            bool full() const { return count.load(std::memory_order_acquire) == Capacity; }
            bool empty() const { return count.load(std::memory_order_acquire) == 0; }

            u16 remaining() const noexcept {
                return count.load(std::memory_order_acquire)
                    - start.load(std::memory_order_acquire);
            }
        };

        struct ChunkDeleter {
            ChunkAllocator* alloc;
            void operator()(Chunk* c) const noexcept {
                alloc->deallocate(c);
            }
        };

        using ChunkHandle = std::unique_ptr<Chunk, ChunkDeleter>;

        static constexpr size_t EffectiveAlign = (Align > alignof(Chunk)) ? Align : alignof(Chunk);

        static constexpr size_t ChunkSize =
            (sizeof(Chunk) + EffectiveAlign - 1) & ~(EffectiveAlign - 1);

        static_assert(ChunkSize >= sizeof(Chunk), "ChunkSize must cover Chunk");
        static_assert((ChunkSize% EffectiveAlign) == 0, "ChunkSize must be multiple of EffectiveAlign");

        static constexpr size_t PageSize = ChunkCount * ChunkSize;
        static_assert(PageSize >= ChunkSize, "PageSize too small");

        static constexpr size_t ChunksPerPage = PageSize / ChunkSize;
        static_assert(ChunksPerPage > 0, "PageSize yields zero chunks");

        static constexpr u16 Invalid = std::numeric_limits<u16>::max();

        ChunkAllocator()
            : heap_(nullptr)
            , freeList_(Invalid)
        {
            static_assert(ChunksPerPage <= std::numeric_limits<u16>::max(), "Index overflow");

            heap_ = static_cast<std::byte*>(
                ::operator new(PageSize, std::align_val_t{ EffectiveAlign }));

            assert(heap_);

            for (u16 i = 0; i < ChunksPerPage; ++i) {
                void* p = heap_ + i * ChunkSize;
                ::new (p) Chunk();
                getChunk(i)->next_ = (i + 1 < ChunksPerPage) ? static_cast<u16>(i + 1) : Invalid;
            }
            freeList_ = 0;
        }

        ~ChunkAllocator() {
            for (size_t i = 0; i < ChunksPerPage; ++i) {
                getChunk(static_cast<u16>(i))->~Chunk();
            }

            ::operator delete(heap_, PageSize, std::align_val_t{ EffectiveAlign });
        }

        ChunkHandle allocateHandle() {
            Chunk* c = allocate();
            if (!c) return nullptr;
            return ChunkHandle(c, ChunkDeleter{ this });
        }

        Chunk* allocate() {

            if (freeList_ == Invalid) {
                return nullptr; // 空きなし
            }

            const u16 index = freeList_;
            Chunk* chunk = getChunk(index);
            freeList_ = chunk->next_;

            chunk->start.store(0, std::memory_order_relaxed);
            chunk->count.store(0, std::memory_order_relaxed);

            return chunk;
        }

        void deallocate(Chunk* chunk) {
            if (!chunk) return;

            chunk->next_ = freeList_;

            const u16 index = getIndex(chunk);
            freeList_ = index;
        }

    private:
        Chunk* getChunk(u16 idx) noexcept {
            return reinterpret_cast<Chunk*>(heap_ + idx * ChunkSize);
        }

        u16 getIndex(Chunk* chunk) {
            auto byteOffset = reinterpret_cast<std::byte*>(chunk) - reinterpret_cast<std::byte*>(heap_);
            return static_cast<u16>(byteOffset / ChunkSize);
        }

    private:
        //Chunk* heap_;
        std::byte* heap_;
        u16 freeList_;
    };

    template<typename ChunkAllocator, typename Chunk = typename ChunkAllocator::Chunk>
    inline auto stealChunkRange(ChunkAllocator& allocator,
        Chunk& chunk, std::uint16_t stealCount)
    {
        // 残りタスク数を取得
        std::uint16_t rem = chunk.remaining();
        if (rem == 0) {
            // ChunkHandle がデフォルト構築で「空」を表すならこれでOK
            return typename ChunkAllocator::ChunkHandle{};
        }

        // 実際に奪う数は残数以下
        std::uint16_t n = std::min(stealCount, rem);

        // 元チャンクの start を advance（フェッチ＆アド）
        std::uint16_t oldStart = chunk.start.fetch_add(n, std::memory_order_acq_rel);

        // 新規ハンドルを確保
        auto handle = allocator.allocateHandle();

        // メタデータを設定
        handle->tasks = chunk.tasks.data();   // ポインタ型 or 参照ラッパー前提
        handle->start = oldStart;
        handle->count = oldStart + n;

        return handle;
    }
}