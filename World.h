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
#include "Entity.h"
#include "EntityPool.h"
#include "Debug.h"
#include "SparseSet.h"
#include "HopscotchHashMap.h"
#include "GroupNode.hpp"
#include "group.hpp"
#include "typeList.hpp"

constexpr size_t MAX_COMPONENTS = 64;

namespace ECS {

class World
{
private:
    template<typename...>
    friend class SceneView;
    
    using base_type = ISparseSet;
    // '1' == active, '0' == inactive.
    using ComponentBitSet = std::bitset<MAX_COMPONENTS>;

    using groupID = EntityIndex;

    //template<typename Type,StorageClass S>
    //using storage_for_type = typename StorageFor<Type, S>::type;

    template <typename Type, GroupType GT = GroupType::basicGroup>
    using GroupClass_t = typename GroupClass<Type, GT>::type;

public:
    World() = default;

    //static std::vector<std::string>m_componentNames;

    EntityID spawnEmpty(const std::string name = "Empty") {
        EntityID id =  entityPool.alloc(name); // EntityPoolを通じてエンティティを作成
        m_entityMasks.Set(id, {});
        return id;
    };

    /*関数使用例
    Position position = Position(0.0f,5.0f);
	auto entity = ECS::world().spawn("",position,Velocity(5.0f,0.1f));
    auto entity2 = ECS::world().spawn("");
    */

    template <typename... Components>
    EntityID spawn(const std::string name = "Object",Components&&... components) {
        EntityID id = entityPool.alloc(name);

        //ComponentData保存&&コンポーネント未登録なら登録
        std::initializer_list<int>{(getComponentPool<std::decay_t<Components>>().Set(id, std::forward<Components>(components)), 0)...};

        //ComponentBitを設定
        m_entityMasks.Set(id, {});
        registComponentSet<Components...>(id);
        return id;
    }

    bool despawn(EntityID& entity){
        if(!entityPool.contains(entity)) return false;

        //Componentの削除処理
        //m_entityMasks.Delete(entity);
        ComponentBitSet& bit = getEntityMask(entity);

        //所持コンポーネントの数
        int compCount = bit.count();

        //全てのコンポーネントデータ削除
        for (int i = 0; i < MAX_COMPONENTS; i++){
            if (bit[i] == 1){
                if (m_componentPools[i]) { 
                    m_componentPools[i]->Delete(entity);
                    compCount--;

                    if(compCount <= 0) break;
                }
            }
        }

        m_entityMasks.Delete(entity);

        return entityPool.dealloc(entity);
    };

    std::string getName(const EntityID& entity){
        if (!entityPool.contains(entity)) return "NULL";

        return entityPool.GetName(entity);
    }

    template <typename T>
    void registerComponent() {
        ASSERT(m_componentPools.size() <= MAX_COMPONENTS,
            "Exceeded max number of registered components");

        size_t index = getComponentIndex<T>();

        if (index >= m_componentPools.size())
            m_componentPools.resize(index + 1);

        ASSERT(!m_componentPools[index],
            "Attempting to register component '" << typeid(T).name() << "' twice");

        m_componentPools[index] = std::make_unique<SparseSet<T>>();

        CUSTOM_INFO("Registered component '" << typeid(T).name() << "'");
    };

    /*関数使用例
    auto component = ECS::world().emplace<Velocity>(entity,1.0f,0.5f);
    */
    template <typename T, typename... Args>
    T* emplace(const EntityID& entityID, Args&&... args) {
        auto& pool = getComponentPool<T>();

        //グループ更新
        registComponentSet<T>(entityID);

        return pool.Set(entityID, std::move(T{ std::forward<Args>(args)... }));
    }

    template <typename T>
    T* getComponent(const EntityID& entityID) {
        auto& pool = getComponentPool<T>();
        return pool.Get(entityID);
    }
    
    template <typename T>
    void removeComponent(const EntityID& entityID){
        //無効なEntity
        if(entityID == INVALID_ENTITY) return;

        auto& pool = getComponentPool<T>();

        if (!pool.Get(entityID)) return;

        pool.Delete(entityID);
        
        //グループ更新
        ComponentBitSet& mask = getEntityMask(entityID);
        setComponentBit<T>(mask, 0);
    }

    ComponentBitSet* getComponentBitSet(const EntityID& entity){
        return m_entityMasks.Get(entity);
    }

    template <typename... Components>
    bool has(EntityID entity){
        auto bitset = getEntityMask(entity);

        if(!bitset.any()) return false;

        ComponentBitSet newMask = getMask<Components...>();
        return ((bitset & newMask) == newMask);
    }
    
    /*
    template <typename... Get, typename... Exclude>
    typename std::enable_if<(sizeof...(Get) > 0), SceneView<get_t<Get...>, exclude_t<Exclude...>>>::type
        View(exclude_t<Exclude...> = exclude_t<>{}) {
        return SceneView<get_t<Get...>, exclude_t<Exclude...>>{};
    }
    */
    
    /*
    template <typename... Get, typename... Exclude>
    typename std::enable_if < (sizeof...(Get) > 0),SceneView<get_t<Get...>, exclude_t<Exclude...>> View(exclude_t<Exclude...> = exclude_t<>{},
        ) {
        return SceneView<get_t<Get...>, exclude_t<Exclude...>>{};
    }
    */

    /*
    template <typename... Get, typename... Exclude>
    SceneView<get_t<Get...>, exclude_t<Exclude...>> View(exclude_t<Exclude...> = exclude_t<>{}) {
        return SceneView<get_t<Get...>, exclude_t<Exclude...>>{};
    }
    */
    /*
    template <typename... Get, typename... Exclude>
    std::enable_if_t<(sizeof...(Get) > 0), SceneView<get_t<Get...>, exclude_t<Exclude...>>>
        View(exclude_t<Exclude...> = exclude_t<>{}) {
        return SceneView<get_t<Get...>, exclude_t<Exclude...>>{};
    }
    */
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
    
    /*
    // Excludeありのオーバーロード
    template <typename... Get, typename... Exclude>
    SceneView<get_t<Get...>, exclude_t<Exclude...>> View(exclude_t<Exclude...>) {
        return SceneView<get_t<Get...>, exclude_t<Exclude...>>{};
    }>
    */

    template<GroupType GT = GroupType::basicGroup,typename Type>
    constexpr auto CreateGroupNode(){
        return std::make_unique<GroupNode<Type>>(getComponentPool<std::remove_const_t<Type>>());
    }

    template<GroupType GT = GroupType::basicGroup,typename... Owned, typename... Get, typename... Exclude>
    Group<owned_t<GroupClass_t<Owned, GT>...>,get_t<GroupClass_t<Get, GT>...>,exclude_t<GroupClass_t<Exclude, GT>...>
    >group(
    get_t<Get...> = get_t{},
    exclude_t<Exclude...> = exclude_t{}) {
        using group_type = Group<owned_t<GroupClass_t<Owned, GT>...>, get_t<GroupClass_t<Get, GT>...>, exclude_t<GroupClass_t<Exclude,GT>...>>;

        using handler_type = typename group_type::handler;

        std::shared_ptr<handler_type> handler{};

        //groupsを見て、存在するか確認。
        if(auto ptr = m_groups.find(group_type::group_id())){
            return group_type{};
        }

        //無いと仮定して、作成
        //Owner未指定の場合は専用の処理
        if constexpr(sizeof...(Owned) == 0u){
            return group_type{};
        }

        /*
        handler = std::make_shared<handler_type>(
            std::tuple_cat(
                std::forward_as_tuple(CreateGroupNode<GT, Owned>()...),
                std::forward_as_tuple(CreateGroupNode<GT, Get>()...)
            ),
            std::forward_as_tuple(CreateGroupNode<GT, Exclude>()...)
            );
            */

        handler = std::make_shared<handler_type>(
            std::tuple_cat(
                std::make_tuple(CreateGroupNode<GT, Owned>()...),
                std::make_tuple(CreateGroupNode<GT, Get>()...)
            ),
            std::make_tuple(CreateGroupNode<GT, Exclude>()...)
            );

        
        m_groups.insert(group_type::group_id(),handler);
        return {*handler};
    }

    size_t getGroupSize() noexcept
    {
        return m_groups.size();
    }

private:
    //EntityIDをSparseSetで再利用できるようにしている.
    //再利用時、ID(EntityIndex(32bit),Version(32bit)が組み合わされて発行される
    EntityPool entityPool;

    //コンポーネントデータが格納される
    //componentのクラスごとにindexが振られ、entityIDとcomponentクラスに対応した値が返される.
    std::vector<std::unique_ptr<ISparseSet>> m_componentPools;

    //各エンティティのComponentBitSet
    SparseSet<ComponentBitSet>m_entityMasks;

    ecs_map::HopscotchHashMap<ecs_map::id_type,std::shared_ptr<IHandler>>m_groups;

    static size_t getNextComponentIndex(const std::string typeName)
    {
        static size_t ind = 0;

        if(ind > MAX_COMPONENTS)
        {
            ASSERT(false,typeName << " Component index over MAX_COMPONENTS " << MAX_COMPONENTS);
        }
        //World::m_componentNames.push_back(typeName);
        return ind++;
    };

    template <typename T>
    static size_t getComponentIndex() {
        static size_t ind = getNextComponentIndex(typeid(T).name());
        return ind;
    };

    template <typename T>
    ComponentBitSet::reference getComponentBit(ComponentBitSet& mask) {
        size_t bitPos = getComponentIndex<T>();
        return mask[bitPos];
    }

    template <typename... Components>
    ComponentBitSet getMask() {
        ComponentBitSet mask;
        std::initializer_list<int>{ (setComponentBit<Components>(mask, 1), 0)... };
        return mask;
    }

    ComponentBitSet& getEntityMask(EntityID entity){
        return *m_entityMasks.Get(entity);
    }

    template <typename... Components>
    void registComponentSet(const EntityID& entity){
        ComponentBitSet* bitset = m_entityMasks.Get(entity);

        //無効なentityを指定した
        if (!bitset) {
            return;
        }

        ComponentBitSet newMask = getMask<Components...>();

        // すべてのコンポーネントがすでに登録されている場合は何もしない
        if ((*bitset & newMask) == newMask) {
            return;
        }

        //コンポーネント登録
        std::initializer_list<int>{(setComponentBit<Components>(*bitset, 1), 0)... };
    }

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

    template <typename T>
    void setComponentBit(ComponentBitSet& bit, bool val) {
        size_t bitPos = getComponentIndex<T>();
        bit[bitPos] = val;
    }

    template <typename T>
    size_t getOrRegisterComponentIndex() {
        size_t index = getComponentIndex<T>();

        if (index >= m_componentPools.size() || !m_componentPools[index])
            registerComponent<T>();

        // Internal error, should never happen outside development
        ASSERT(index < m_componentPools.size() && index >= 0,
            "Type index out of bounds for component '" << typeid(T).name() << "'");

        return index;
    };

    template <typename T>
    ISparseSet* getComponentPoolPtr() {
        size_t index = getOrRegisterComponentIndex<T>();
        return m_componentPools[index].get();
    };
    
    template <typename T>
    SparseSet<T>& getComponentPool() {
        ISparseSet* genericPtr = getComponentPoolPtr<T>();
        SparseSet<T>* pool = static_cast<SparseSet<T>*>(genericPtr);

        return *pool;
    };
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

