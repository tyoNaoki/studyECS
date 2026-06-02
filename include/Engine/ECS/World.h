#pragma once
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <tuple>
#include <functional>
#include <algorithm>
#include <vector>
#include <bitset>
#include <type_traits>
#include <utility>

#include "TestFramework.hpp"
#include "Entity.h"
#include "EntityPool.h"
#include "SparseSet.h"
#include "Engine/Containers/HopscotchHashMap.h"
#include "Storage.hpp"
#include "group.hpp"
#include "typeList.hpp"
#include "ComponentPoolManager.hpp"
//#include "Engine\ECS\System\SystemBase.hpp"
#include "Engine\ECS\System\SystemGroup.h"
#include "Engine\ECS\System\SystemScheduler.h"
#include "Engine\ECS\Events\IEvent.hpp"
#include "Engine\ECS\Events\EventDispatcherST.hpp"

constexpr size_t MAX_COMPONENTS = 64;



namespace ECS {

struct TimeData {
    float deltaTime = 0.0f;
    float fixedDeltaTime = 1.0f / 50.0f; // 50Hz
    float accumulator = 0.0f;
    float time = 0.0f;
};

struct ISingletonHolder {
	virtual ~ISingletonHolder() = default;
};

template<typename T>
struct SingletonHolder : public ISingletonHolder {
    T value;

    SingletonHolder(const T& v) : value(v) {}
};

class World
{
private:
    template<typename...>
    friend class SceneView;
    
    using base_type = ISparseSet;
    
    using groupID = uint32_t;

    //template<typename Type,StorageClass S>
    //using storage_for_type = typename StorageFor<Type, S>::type;

public:
    World() = default;

    // ムーブを許可
    World(World&&) noexcept = default;
    World& operator=(World&&) noexcept = default;

    // コピーは禁止
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    void initialize(){
        systemScheduler.initialize(*this);
    }

    //static std::vector<std::string>m_componentNames;

    template<typename T,typename Tag>
    ECS::System::SystemHandle registerSystemGroup() {
        auto index = createSystemGroup<T>();
        auto handle = createSystemInfo(index,true);
        //コンストラクター
        getSystemGroup(index)->onCreate(*this);

        tagToGroup[typeid(Tag)] = index;
        return handle;
    }

    template<typename SystemT,typename Tag>
    ECS::System::SystemHandle registerSystem()
    {
        auto it = tagToGroup.find(typeid(Tag));
        if (it == tagToGroup.end()) {
            ASSERT(false, "Group not found for tag");
        }

        size_t groupIndex = it->second;
        auto index = createSystem<SystemT>();
        auto handle = createSystemInfo(index, false);
        getSystem(index)->onCreate(*this);

        systemGroups[groupIndex]->addSystem(handle);
        return handle;
    }

    ECS::System::SystemEntry& getSystemEntry(ECS::System::SystemHandle handle) {
        return systemInfos[handle.ID];
    }

    ECS::System::SystemBase* getSystem(size_t index) {
        return systemBases[index].get();
    }

    ECS::System::SystemGroup* getSystemGroup(size_t index) {
        return systemGroups[index].get();
    }

    void addAfter(ECS::System::SystemHandle before, ECS::System::SystemHandle after){
        auto& sBefore = getSystemEntry(before);
        auto& sAfter = getSystemEntry(after);

        // before → after
        sBefore.after.push_back(after);
        sAfter.before.push_back(before);
    }

    void addBefore(ECS::System::SystemHandle before, ECS::System::SystemHandle after) {
        auto& sBefore = getSystemEntry(before);
        auto& sAfter = getSystemEntry(after);

        // before → after
        sAfter.before.push_back(before);
        sBefore.after.push_back(after);
    }
    
    //Entity作製時に何か関数を紐づけたい場合
    auto& entityPoolConstructor() {
        return entityPool.on_construct();
    }

    //Entity削除時に何か関数を紐づけたい場合
    auto& entityPoolDestructor() {
        return entityPool.on_destroy();
    }

    Entity::EntityID spawnEmpty(const std::string name = "Empty") {
        Entity::EntityID id =  entityPool.alloc(name); // EntityPoolを通じてエンティティを作成
        componentPoolManager.setEntityMask(id);

        entityPool.notify_construct(id);
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

    //期待コンポーネント …Components とユーザが渡した Provided…
    //足りない型は {} で補完
    template <typename... Components, typename... ProvidedArgs>
    Entity::EntityID spawn(const std::string& name, ProvidedArgs&&... provided) {
        auto provTuple = std::make_tuple(std::forward<ProvidedArgs>(provided)...);
        auto fullTuple = std::make_tuple(extract_or_default<Components>(provTuple)...);

        return spawn_impl<Components...>(name, std::move(fullTuple));
    }

       //名前省略オーバーロード
    template <typename... Components, typename... ProvidedArgs,
        std::enable_if_t<(sizeof...(ProvidedArgs) <= sizeof...(Components)), int> = 0>
        Entity::EntityID spawn(ProvidedArgs&&... provided) {
        return spawn<Components...>("Object", std::forward<ProvidedArgs>(provided)...);
    }

    bool despawn(Entity::EntityID& entity){
        if(!entityPool.contains(entity)) return false;

        //デストラクター
        entityPool.notify_destroy(entity);

        //コンポーネント削除
        componentPoolManager.removeAllComponent(entity);
        //entityMaskを削除
        componentPoolManager.deleteEntity(entity);
       //freeIDに追加
        return entityPool.dealloc(entity);
    };

    std::string getName(const Entity::EntityID& entity){
        if (!entityPool.contains(entity)) return "NULL";

        return entityPool.GetName(entity);
    }

    /*関数使用例
    auto component = ECS::world().emplaceOrUpdateComponent<Velocity>(entity,1.0f,0.5f);
    */

    template <typename T, typename... Args>
    T* emplaceOrUpdateComponent(const Entity::EntityID& entityID, Args&&... args) {
        return componentPoolManager.emplaceOrUpdateComponent<T>(entityID, std::forward<Args>(args)...
            );
    }

    template <typename T, typename... Args>
    T* emplace(const Entity::EntityID& entityID, Args&&... args) {
        return componentPoolManager.emplace<T>(entityID, std::forward<Args>(args)...
            );
    }

    template<typename T>
    size_t getOrRegisterComponentIndex(){
        return componentPoolManager.getOrRegisterComponentIndex<T>();
    }

    template <typename T>
    T* getComponent(const Entity::EntityID& entityID) {
        return componentPoolManager.getComponent<T>(entityID);
    }
    
    template <typename T>
    void removeComponent(const Entity::EntityID& entityID){
        componentPoolManager.removeComponent<T>(entityID);
    }

    void removeAllComponent(const Entity::EntityID& entityID) {
        componentPoolManager.removeAllComponent(entityID);
    }

    auto* getComponentBitSet(const Entity::EntityID& entity){
        return componentPoolManager.getComponentBitSet(entity);
    }

    template <typename... Components>
    bool has(const Entity::EntityID entity){
        return componentPoolManager.has<Components...>(entity);
    }

    auto& worldEvent(){
        return worldEvents;
    }

    template <typename... Get>
    SceneView<Get...>
        View() {
        ASSERT(sizeof...(Get) > 0, "Get... must not be empty!");

        return SceneView<Get...>(*this);
    }

    template <typename... Get, typename... Ex>
    SceneView<Get...> View(exclude_t<Ex...>) {
        ASSERT(sizeof...(Get) > 0, "Get... must not be empty!");

        std::vector<ISparseSet*> excludedPools = { getComponentPoolPtr<Ex>()... };
        return SceneView<Get...>(*this, excludedPools);
    }
    
    template<typename... Owned, typename... Get, typename... Exclude, COMPONENT::StorageType S = COMPONENT::StorageType::EventType>
    Group<owned_t<COMPONENT::StorageClass_t<Owned, S>...>,get_t<COMPONENT::StorageClass_t<Get, S>...>,exclude_t<COMPONENT::StorageClass_t<Exclude, S>...>>
    group(get_t<Get...> = get_t{},exclude_t<Exclude...> = exclude_t{}) {
        using group_type = Group<owned_t<COMPONENT::StorageClass_t<Owned, S>...>, get_t<COMPONENT::StorageClass_t<Get, S>...>, exclude_t<COMPONENT::StorageClass_t<Exclude,S>...>>;

        using handler_type = typename group_type::handler;

        std::shared_ptr<handler_type> handler{};

        //groupsを見て、存在するか確認。
        if(auto ptr = groups.find(group_type::group_id())){
            ASSERT(false,"this groups is valid");
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
            ASSERT(std::all_of(groups.cbegin(), groups.cend(),[](const auto data) { return !(data->owned(ecs_map::type_hash<Owned>()) || ...); }), "Conflicting groups");
        }
       
        groups.insert(group_type::group_id(),handler);
        return {*handler};
    }

    size_t getGroupSize() noexcept
    {
        return groups.size();
    }
    

    template <typename T>
    auto& getComponentPool() {
        return componentPoolManager.getComponentPool<T>();
    };

    template <typename T>
    ISparseSet* getComponentPoolPtr() {
        return componentPoolManager.getComponentPoolPtr<T>();
    };

    void update(float dt) {
        time.deltaTime = dt;
        time.time += dt;

		systemScheduler.onUpdate(*this);
    }

    void render() {
        systemScheduler.onRender(*this);
	}

    void cleanup() {
        systemScheduler.onCleanup(*this);
	}

    TimeData& getTime(){
        return time;
    }

private:
    template<typename T>
    size_t createSystem() {
        auto index = systemBases.size();
        systemBases.push_back(std::make_unique<T>());

        return index;
    }

    template<typename T>
    size_t createSystemGroup() {
        auto index = systemGroups.size();
        systemGroups.push_back(std::make_unique<T>());

        return index;
    }

    ECS::System::SystemHandle createSystemInfo(size_t index, bool isGroup) {
        size_t id = systemInfos.size();
        systemInfos.emplace_back();
        systemInfos[id].index = index;
        systemInfos[id].isGroup = isGroup;

        return ECS::System::SystemHandle{id};
    }

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
    Entity::EntityID spawn_impl(const std::string& name, Tuple&& fullTuple) {
        Entity::EntityID id = entityPool.alloc(name);
        componentPoolManager.setEntityMask(id);

        std::apply(
            [&](auto&&... comps) {
                (emplace<std::decay_t<decltype(comps)>>(
                    id, std::forward<decltype(comps)>(comps)), ...);
            },
            std::forward<Tuple>(fullTuple));

        entityPool.notify_construct(id);
        return id;
    }

    template<typename T>
    void setSingleton(const T& value) {
        singletons[typeid(T)] = std::make_unique<SingletonHolder<T>>(value);
    }

	template<typename T>
    T& getSingleton() {
        auto it = singletons.find(typeid(T));
        if (it == singletons.end()) {
            ASSERT(false,"Singleton not found");
        }
        return static_cast<SingletonHolder<T>*>(it->second.get())->value;
	}

private:
    TimeData time;
    //EntityIDをSparseSetで再利用できるようにしている.
    //再利用時、ID(EntityIndex(32bit),Version(32bit)が組み合わされて発行される
    EntityPool entityPool;

    //Entity毎のコンポーネントを管理する
    COMPONENT::ComponentPoolManager<MAX_COMPONENTS> componentPoolManager;

    //システム
    std::vector<ECS::System::SystemEntry> systemInfos;
    std::vector<std::unique_ptr<ECS::System::SystemGroup>>systemGroups;
    std::vector<std::unique_ptr<ECS::System::SystemBase>>systemBases;
    std::unordered_map<std::type_index, size_t> tagToGroup;

    //中枢システムグループをまとめたもの
    ECS::System::SystemScheduler systemScheduler;

    //ECSのコンポーネントを回すためのグループ
    ecs_map::HopscotchHashMap<ecs_map::id_type,std::shared_ptr<IHandler>>groups;

    ECS::EVENT::EventDispatcher_Single<EVENT::WorldEventType, void(const EVENT::EventPointer&),EVENT::EventPolicy> worldEvents;

	std::unordered_map<std::type_index, std::unique_ptr<ISingletonHolder>> singletons;

    //引数は絶対にBorrowで渡す
    //appendListner<Borrow<const T>><(BorrowMut<T>>(HashID,std::funciton([参照する変数](template<>に合わせる)))
    //publish(クラスハッシュID + &item)
    //dispatch(),dispatchOne,dispatch(ハッシュID)
    
    //EVENT::Signal m_WorldEvents;

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
    struct Pack
    {
        Pack(Entity::EntityID entityID, std::tuple<Get&...> comps):entity(entityID),components(comps){}
        Entity::EntityID entity;
        std::tuple<Get&...> components;
    };

    //using Pack = std::tuple<EntityID, Get&...>;
    using componentTypes = type_list<Get...>;

    std::array<ISparseSet*, sizeof...(Get)> m_viewPools;
    std::vector<ISparseSet*> m_excludedPools;
    //std::vector<Pack<Get...>>packedEntities;
    std::vector<Pack>packedEntities;
    World& world;

    // Sparse set with the smallest number of components,
    // basis for ForEach iterations.
    ISparseSet* m_smallest = nullptr;
    
private:
    //対象のコンポーネントを全て所持しているか
    bool AllContain(Entity::EntityID id) {
        return std::all_of(m_viewPools.begin(), m_viewPools.end(), [id](ISparseSet* pool) {
            return pool->ContainsEntity(id);
            });
    }

    //除外対象のコンポーネントを含んでいないか
    bool NotExcluded(Entity::EntityID id) {
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
    auto MakeComponentTuple(Entity::EntityID id, std::index_sequence<Indices...>) {
        return std::make_tuple(std::ref(GetPoolAt<Indices>()->GetRef(id))...);
    }

    template <typename Func>
    void ForEachImpl(Func func) {
        constexpr auto inds = std::make_index_sequence<sizeof...(Get)>{};

        // 最も小さいコンポーネントプールを走査し、他のプールと比較する
        // エンティティリストをコピーすることで、ループ中の安全な削除を可能にする
        for (Entity::EntityID id : m_smallest->GetEntityList()) {
            if (AllContain(id) && NotExcluded(id)) {

                // 関数適用（エンティティIDを含む場合）
                if constexpr (std::is_invocable_v<Func, Entity::EntityID, Get&...>) {
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
    using ForEachFuncWithID = std::function<void(Entity::EntityID, Get&...)>;

    //using Iterator = std::vector<EntityID>;
    //iterator begin() { return m_entities.begin(); }
    //iterator end() { return m_entities.end(); }

    SceneView() = delete;

    SceneView(ECS::World& world_) : world(world_),m_viewPools{ world_.getComponentPoolPtr<Get>()... }
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
        : m_smallest(other.m_smallest),m_viewPools(other.m_viewPools), m_excludedPools(excludedPools),world(other.world){

        createPacked();
    }

    // example
    // auto view = world.View<StartSpawnCubeEvent>().Exclude<StartTag>();
    //取得しいるコンポーネントEntityをさらに絞り込む
    template <typename... ExcludedComponents>
    SceneView Exclude() {
        std::vector<ISparseSet*> excludedPools = { world.getComponentPoolPtr<ExcludedComponents>()... };

        return SceneView(*this, excludedPools);
    }
    
    /* 以下関数使用例
    *   auto view = ECS::world().View<Position,Velocity>();
    *   for(auto& x: view){
    *       //EntitiyIDは変数で取得
			auto& entityID = x.entity;
            //packにあるcomponentは以下関数で取得
			auto& vel = view.get<Velocity>(x.components);
			auto& posi = view.get<Position>(x.components);
            vel.x += 5.0f;
	    }
    */

    template <typename Type>
    Type& get(std::tuple<Get&...>& pack) {
        return std::get<Type&>(pack);
    }

    size_t size(){
        return m_smallest->Size();
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

    std::vector<std::tuple<Entity::EntityID, Get&...>> each() {
        constexpr auto inds = std::make_index_sequence<sizeof...(Get)>{};
        //std::vector<Pack<Get...>> result;
        std::vector<std::tuple<Entity::EntityID, Get&...>> result;

        for (Entity::EntityID id : m_smallest->GetEntityList()) {
            if (AllContain(id) && NotExcluded(id)) {
                result.push_back(std::tuple_cat(std::make_tuple(id), MakeComponentTuple(id, inds)));
            }
        }

        return result;
    }


    /*以下関数使用例
    * auto view = ECS::world().View<Position,Velocity>();
      view.each([](auto entity,auto &pos,auto &vel){
			pos.x+=5.0f;
			vel.x += 5.0f;
		});

		view.each([](auto& pos, auto& vel) {
			pos.x += 5.0f;
			vel.x += 5.0f;
		});
    */
    
    template <typename Func>
    void each(Func func){
        constexpr auto inds = std::make_index_sequence<sizeof...(Get)>{};

        for(Entity::EntityID entity : m_smallest->GetEntityList()){
            if(!AllContain(entity)||!NotExcluded(entity)) continue;

            auto component_Tuple = MakeComponentTuple(entity, inds);
            if constexpr (std::is_invocable_v<Func, Entity::EntityID,Get&...>){
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

