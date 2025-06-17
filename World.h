#pragma once
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <tuple>
#include <functional>
#include <algorithm>
#include <mutex>
#include<vector>
#include <bitset>
#include <type_traits>
#include <utility>
#include "Entity.h"
#include "EntityPool.h"
#include "Debug.h"
#include "SparseSet.h"
#include "HopscotchHashMap.h"
#include "Storage.hpp"
#include "group.hpp"
#include "typeList.hpp"
#include "ComponentPoolManager.hpp"
#include "EventQueue.hpp"
#include "BorrowDispatcher.hpp"

constexpr size_t MAX_COMPONENTS = 64;

namespace ECS {
    
class World
{
private:
    template<typename...>
    friend class SceneView;
    
    using base_type = ISparseSet;
    
    using groupID = EntityIndex;

    //template<typename Type,StorageClass S>
    //using storage_for_type = typename StorageFor<Type, S>::type;

public:
    World() = default;

    //static std::vector<std::string>m_componentNames;

    EntityID spawnEmpty(const std::string name = "Empty") {
        EntityID id =  entityPool.alloc(name); // EntityPoolを通じてエンティティを作成
        componentPoolManager.setEntityMax(id);
        return id;
    };

    /*関数使用例
    Position position = Position(0.0f,5.0f);
	auto entity = ECS::world().spawn("",position,Velocity(5.0f,0.1f));
    auto entity2 = ECS::world().spawn("");
    auto entity3 = ECS::world().spawn<A,B>(A{});
    auto entity4 = ECS::world().spawn<A, B>(B{});
    auto entity5 = ECS::world().spawn<A, B>(A{},B{});
    auto entity6 = ECS::world().spawn<A, B>("Apple", A{}, B{});
    auto entity6 = ECS::world().spawn<A, B>(B{});
    auto entity7 = ECS::world().spawn<A, B>("Banana", B{});
    */

    //tuple から Expected に変換可能な最初の要素を返し、
    //見つからなければ Expected{} を返す再帰テンプレート
    template <typename Expected, typename Tuple, std::size_t I = 0>
    constexpr Expected extract_or_default(const Tuple& tup) {
        if constexpr (I == std::tuple_size_v<Tuple>) {
            // 最後まで探しても無ければデフォルト
            return Expected{};
        }
        else if constexpr (
            std::is_convertible_v<
            std::tuple_element_t<I, Tuple>, Expected>) {
            // I 番目の要素が変換可能ならそれを採用
            return static_cast<Expected>(std::get<I>(tup));
        }
        else {
            // 見つかるまで再帰的に次へ
            return extract_or_default<Expected, Tuple, I + 1>(tup);
        }
    }

     //各コンポーネント値が揃ったタプルを受け取って実際に登録
    template <typename... Components, typename Tuple>
    EntityID spawn_impl(const std::string& name, Tuple&& fullTuple) {
        EntityID id = entityPool.alloc(name);
        componentPoolManager.setEntityMax(id);

        std::apply(
            [&](auto&&... comps) {
                (emplace<std::decay_t<decltype(comps)>>(
                    id, std::forward<decltype(comps)>(comps)), ...);
            },
            std::forward<Tuple>(fullTuple));

        return id;
    }

    //期待コンポーネント …Components とユーザが渡した Provided…
    //足りない型は {} で補完
    template <typename... Components, typename... ProvidedArgs>
    EntityID spawn(const std::string& name, ProvidedArgs&&... provided) {
        auto provTuple = std::make_tuple(std::forward<ProvidedArgs>(provided)...);
        auto fullTuple = std::make_tuple(extract_or_default<Components>(provTuple)...);
        return spawn_impl<Components...>(name, std::move(fullTuple));
    }

       //名前省略オーバーロード
    template <typename... Components, typename... ProvidedArgs,
        std::enable_if_t<(sizeof...(ProvidedArgs) <= sizeof...(Components)), int> = 0>
        EntityID spawn(ProvidedArgs&&... provided) {
        return spawn<Components...>("Object", std::forward<ProvidedArgs>(provided)...);
    }

    bool despawn(EntityID& entity){
        if(!entityPool.contains(entity)) return false;

       componentPoolManager.deleteAllComponent(entity);

       componentPoolManager.deleteEntity(entity);

        return entityPool.dealloc(entity);
    };

    std::string getName(const EntityID& entity){
        if (!entityPool.contains(entity)) return "NULL";

        return entityPool.GetName(entity);
    }

    /*関数使用例
    auto component = ECS::world().emplace<Velocity>(entity,1.0f,0.5f);
    */
    template <typename T, typename... Args>
    T* emplace(const EntityID& entityID, Args&&... args) {
        return componentPoolManager.emplace<T>(entityID,args...);
    }

    template <typename T>
    T* getComponent(const EntityID& entityID) {
        return componentPoolManager.getComponent<T>(entityID);
    }
    
    template <typename T>
    void removeComponent(const EntityID& entityID){
        componentPoolManager.removeComponent<T>(entityID);
    }

    auto* getComponentBitSet(const EntityID& entity){
        return componentPoolManager.getComponentBitSet(entity);
    }

    template <typename... Components>
    bool has(EntityID entity){
        return componentPoolManager.has<Components...>(entity);
    }

    auto& worldEvent(){
        return m_WorldEvents;
    }
    
    template <typename... Get>
    std::unique_ptr<SceneView<Get...>>  
    View() {
        if constexpr (sizeof...(Get) > 0) {
            return std::make_unique<SceneView<Get...>>();
        }
        else {
            ASSERT(sizeof...(Get) > 0, "Get... must not be empty!");
        }
    }

    template<StorageType S = StorageType::EventType,typename... Owned, typename... Get, typename... Exclude>
    Group<owned_t<StorageClass_t<Owned, S>...>,get_t<StorageClass_t<Get, S>...>,exclude_t<StorageClass_t<Exclude, S>...>>
    group(get_t<Get...> = get_t{},exclude_t<Exclude...> = exclude_t{}) {
        using group_type = Group<owned_t<StorageClass_t<Owned, S>...>, get_t<StorageClass_t<Get, S>...>, exclude_t<StorageClass_t<Exclude,S>...>>;

        using handler_type = typename group_type::handler;

        std::shared_ptr<handler_type> handler{};

        //groupsを見て、存在するか確認。
        if(auto ptr = m_groups.find(group_type::group_id())){
            return group_type{};
        }

        //無いと仮定して、作成
        //Owner未指定の場合は専用の処理
        if constexpr(sizeof...(Owned) == 0u){
            handler = std::make_shared<handler_type>(
                std::forward_as_tuple(getComponentPool<Get>()...),
                std::forward_as_tuple(getComponentPool<Exclude>()...)
                );
        }else{
            handler = std::make_shared<handler_type>(
                // Owned + Get 用
                std::tuple_cat(
                    std::forward_as_tuple(getComponentPool<Owned>()...),
                    std::forward_as_tuple(getComponentPool<Get>()...)
                ),
                // Exclude 用
                std::forward_as_tuple(getComponentPool<Exclude>()...)
                );
            ASSERT(std::all_of(m_groups.cbegin(),m_groups.cend(),[](const auto data) { return !(data->owned(ecs_map::type_hash<Owned>()) || ...); }), "Conflicting groups");
        }
       
        m_groups.insert(group_type::group_id(),handler);
        return {*handler};
    }

    size_t getGroupSize() noexcept
    {
        return m_groups.size();
    }

    template <typename T>
    auto& getComponentPool() {
        return componentPoolManager.getComponentPool<T>();
    };

    template <typename T>
    ISparseSet* getComponentPoolPtr() {
        return componentPoolManager.getComponentPoolPtr<T>();
    };

private:
    //EntityIDをSparseSetで再利用できるようにしている.
    //再利用時、ID(EntityIndex(32bit),Version(32bit)が組み合わされて発行される
    EntityPool entityPool;

    COMPONENT::ComponentPoolManager<MAX_COMPONENTS> componentPoolManager;

    //コンポーネントデータが格納される
    //componentのクラスごとにindexが振られ、entityIDとcomponentクラスに対応した値が返される.
    //std::vector<std::unique_ptr<ISparseSet>> m_componentPools;

    //各エンティティのComponentBitSet
    //SparseSet<ComponentBitSet>m_entityMasks;

    ecs_map::HopscotchHashMap<ecs_map::id_type,std::shared_ptr<IHandler>>m_groups;

    //引数は絶対にBorrowで渡す
    //appendListner<Borrow<const T>><(BorrowMut<T>>(HashID,std::funciton([参照する変数](template<>に合わせる)))
    //publish(クラスハッシュID + &item)
    //dispatch(),dispatchOne,dispatch(ハッシュID)
    EVENT::Signal m_WorldEvents;

    /*
    template <typename... EntityIDs>
    void setComponentGroup(const ComponentBitSet& bit,EntityIDs... entities) {
        std::initializer_list<int>{ (m_groups[bit].push_back(entities), 0)... };
    }

    template <typename... EntityIDs>
    void removeComponentGroup(const ComponentBitSet& bit, EntityIDs... entities) {
        auto& group = m_groups[bit]; 

        for (auto entity : { entities... }) {
            group.erase(std::remove(group.begin(), group.end(), entity), group.end());
        }

        //空なら削除する
        if(group.empty())
        {
            m_groups.erase(bit);
        }
    }
    */

    /*
    template <typename... EntityIDs>
    void setEntitisOnComponentGroup(ComponentBitSet& bit, EntityIDs... entities) {
        (m_groups[bit].push_back(entities), ...); 
    }

    template <typename... EntityIDs>
    void removeEntitisOnComponentGroup(ComponentBitSet& bit, EntityIDs... entities) {
        (m_groups[bit].erase(std::remove(m_groups[bit].begin(), m_groups[bit].end(), entities), m_groups[bit].end()), ...);
    }
    */
    
   
};

static World& world() {
    static World sWorld;
    static std::mutex mutex;

    std::lock_guard<std::mutex> lock(mutex); // スレッドセーフ
    return sWorld;
}


template<typename Pack>
class SceneViewIterator {
private:
    typename std::vector<Pack>::iterator current;

public:
    SceneViewIterator(typename std::vector<Pack>::iterator it) : current(it) {}

    Pack& operator*() { return *current; }
    SceneViewIterator& operator++() { ++current; return *this; }
    bool operator!=(const SceneViewIterator& other) const { return current != other.current; }
};

template<typename... Get>
class SceneView{
private:
    struct Pack
    {
        Pack(EntityID entityID, std::tuple<Get&...> comps):entity(entityID),components(comps){}
        EntityID entity;
        std::tuple<Get&...> components;
    };

    //using Pack = std::tuple<EntityID, Get&...>;
    using componentTypes = type_list<Get...>;

    std::array<ISparseSet*, sizeof...(Get)> m_viewPools;
    std::vector<ISparseSet*> m_excludedPools;
    //std::vector<Pack<Get...>>packedEntities;
    std::vector<Pack>packedEntities;

    // Sparse set with the smallest number of components,
    // basis for ForEach iterations.
    ISparseSet* m_smallest = nullptr;
    
    //対象のコンポーネントを全て所持しているか
    bool AllContain(EntityID id) {
        return std::all_of(m_viewPools.begin(), m_viewPools.end(), [id](ISparseSet* pool) {
            return pool->ContainsEntity(id);
            });
    }

    //除外対象のコンポーネントを含んでいないか
    bool NotExcluded(EntityID id) {
        if (m_excludedPools.empty()) return true;
        return std::none_of(m_excludedPools.begin(), m_excludedPools.end(), [id](ISparseSet* pool) {
            return pool->ContainsEntity(id);
            });
    }

    // インデックスを使用して汎用プール配列を特定し、特定のコンポーネントプールにダウンキャストする
    template <size_t Index>
    auto GetPoolAt() {
        using componentType = typename componentTypes::template get<Index>;
        return static_cast<SparseSet<componentType>*>(m_viewPools[Index]);
    }

    // エンティティIDを指定し、対象のコンポーネントのタプルを作成する
    template <size_t... Indices>
    auto MakeComponentTuple(EntityID id, std::index_sequence<Indices...>) {
        return std::make_tuple(std::ref(GetPoolAt<Indices>()->GetRef(id))...);
    }

    template <typename Func>
    void ForEachImpl(Func func) {
        constexpr auto inds = std::make_index_sequence<sizeof...(Get)>{};

        // 最も小さいコンポーネントプールを走査し、他のプールと比較する
        // エンティティリストをコピーすることで、ループ中の安全な削除を可能にする
        for (EntityID id : m_smallest->GetEntityList()) {
            if (AllContain(id) && NotExcluded(id)) {

                // 関数適用（エンティティIDを含む場合）
                if constexpr (std::is_invocable_v<Func, EntityID, Get&...>) {
                    std::apply(func, std::tuple_cat(std::make_tuple(id), MakeComponentTuple(id, inds)));
                }
                else {
                    ASSERT(false, "関数の引数が適切ではありません");
                }
            }
        }
    }

    //除外対象のコンポーネントを除く、templateで指定したコンポーネントのPoolに含まれているEntityを配列に入れる
    void createPacked()
    {
        // エンティティのフィルタリング処理
        constexpr auto inds = std::make_index_sequence<sizeof...(Get)>{};  // インデックスシーケンスを作

        for (auto ent : m_smallest->GetEntityList()) {
            if (AllContain(ent) && NotExcluded(ent)) {
                //m_entities.push_back(ent);  // 既存のエンティティリストに追加
                packedEntities.push_back({ent, MakeComponentTuple(ent, inds)});
            }
        }
    }

public:

    //functions
    using ForEachFunc = std::function<void(Get&...)>;
    using ForEachFuncWithID = std::function<void(EntityID, Get&...)>;

    //using Iterator = std::vector<EntityID>;
    //iterator begin() { return m_entities.begin(); }
    //iterator end() { return m_entities.end(); }

    SceneView() : m_viewPools{ ECS::world().getComponentPoolPtr<Get>()... }
    {
        //ASSERT(componentTypes::size != m_viewPools.size(), "Component type list and pool array size mismatch");
        // 最小のプールを探す
        auto smallestPool = std::min_element(m_viewPools.begin(), m_viewPools.end(),
            [](ISparseSet* a, ISparseSet* b) { return a->Size() < b->Size(); }
        );

        ASSERT(smallestPool != m_viewPools.end(), "Initializing invalid/empty view");
        m_smallest = *smallestPool;

        // エンティティのフィルタリング処理
        constexpr auto inds = std::make_index_sequence<sizeof...(Get)>{};  

        createPacked();
    }

    SceneView(const SceneView& other, std::vector<ISparseSet*> excludedPools)
        : m_smallest(other.m_smallest),m_viewPools(other.m_viewPools), m_excludedPools(excludedPools) {

        createPacked();
    }

    //取得しいるコンポーネントEntityをさらに絞り込む
    template <typename... ExcludedComponents>
    std::unique_ptr<SceneView> Exclude() {
        std::vector<ISparseSet*> excludedPools = { world().getComponentPoolPtr<ExcludedComponents>()... };

        return std::make_unique<SceneView>(*this, excludedPools);
    }
    
    /* 以下関数使用例
    *   auto view = ECS::world().View<Position,Velocity>();
    *   for(auto& x: *view){
    *       //EntitiyIDは変数で取得
			auto& entityID = x.entity;
            //packにあるcomponentは以下関数で取得
			auto& vel = view->get<Velocity>(x);
			auto& posi = view->get<Position>(x);
            vel.x += 5.0f;
	    }
    */

    template <typename Type>
    Type& get(std::tuple<Get&...>& pack) {
        return std::get<Type&>(pack);
    }

    void ForEach(ForEachFunc func) {
        ForEachImpl(func);
    }

    void ForEach(ForEachFuncWithID func) {
        ForEachImpl(func);
    }
    
    using Iterator = SceneViewIterator<Pack>;

    Iterator begin() { return Iterator(packedEntities.begin()); }
    Iterator end() { return Iterator(packedEntities.end()); }

    /* 以下関数使用例
    * auto view = ECS::world().View<Position,Velocity>();
    for (auto [entity, pos, vel] : view.each()) {
        vel.x += 5f;
    }
    */
    //Pakc<Get...>

    std::vector<std::tuple<EntityID, Get&...>> each() {
        constexpr auto inds = std::make_index_sequence<sizeof...(Get)>{};
        //std::vector<Pack<Get...>> result;
        std::vector<std::tuple<EntityID, Get&...>> result;

        for (EntityID id : m_smallest->GetEntityList()) {
            if (AllContain(id)) {
                result.push_back(std::tuple_cat(std::make_tuple(id), MakeComponentTuple(id, inds)));
            }
        }

        return result;
    }

    /*以下関数使用例
    * auto view = ECS::world().View<Position,Velocity>();
      view->each([](auto entity,auto &pos,auto &vel){
			pos.x+=5.0f;
			vel.x += 5.0f;
		});

		view->each([](auto& pos, auto& vel) {
			pos.x += 5.0f;
			vel.x += 5.0f;
		});
    */
    
    template <typename Func>
    void each(Func func){
        constexpr auto inds = std::make_index_sequence<sizeof...(Get)>{};

        for(EntityID entity : m_smallest->GetEntityList()){
            if(!AllContain(entity)) continue;
            auto component_Tuple = MakeComponentTuple(entity, inds);
            if constexpr (std::is_invocable_v<Func,EntityID,Get&...>){
                std::apply(func, std::tuple_cat(std::make_tuple(entity), component_Tuple));
            }
            else if constexpr (std::is_invocable_v<Func,Get&...>){
                std::apply(func, component_Tuple);
            }else{
                ASSERT(false, "Invalid lambda function passed to view.each()");
            }
        }
    }

};


}//namespace ECS

