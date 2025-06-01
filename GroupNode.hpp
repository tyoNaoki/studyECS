#ifndef ECS_GROUPNODE_HPP
#define ECS_GROUPNODE_HPP

#include "Entity.h"
#include "SparseSet.h"

namespace ECS{
// 1. エンティティの型定義（基本は独自の EntityType を利用）
enum class GroupType{
    basicGroup,
    customGroup
};

class BaseGroupNode {
public:
    virtual ~BaseGroupNode() = default;
};

// コンポーネントの基本ストレージ
// Type は格納対象のコンポーネント型。
//Entityは共通の型としてすべてのT型ストレージを共通のストレージとして運用させるため
template<typename Type>
class GroupNode : public BaseGroupNode{
private:
    SparseSet<Type>*sparseSet;

public:
    using BaseType = BaseGroupNode;
    using type = Type;

    // コンストラクタ：外部のSparseSet<T> をラップ
    explicit GroupNode(SparseSet<Type>& externalSparseSet)
        : sparseSet(&externalSparseSet) {}

    size_t Size(){
        if(!sparseSet) return -1;

        return sparseSet->Size();
    }

    template<typename T>
    bool operator!=(const T& other) const {
        return sparseSet != other;
    }

    // 任意のポインタと比較できるようにオーバーロード
    bool operator!=(const void* other) const {
        return sparseSet != other;
    }

    //using element = BasicStorage<Type,Entity>;
    // 拡張性を考慮して、格納する型もエイリアス化しておく
    // ここに共通処理を追加してゆく（例えばシリアライズ、デバッグ表示など）
};

template<typename Type>
class CustomGroupNode : public GroupNode<Type> {
public:
    using BaseType = BaseGroupNode;
    using type = Type;
    //using type = Type;
    //using element = CustomStorage<Type>;

};

//    将来的に型ごとに別のストレージ（例: EventStorage）を選びたい場合、ここを特殊化すればよい
template<typename Type, GroupType GT = GroupType::basicGroup>
struct GroupClass {
    using type = GroupNode<Type>;
};

template<typename Type>
struct GroupClass<Type, GroupType::customGroup> {
    using type = CustomGroupNode<Type>;
};

}//namespace ECS

#endif
