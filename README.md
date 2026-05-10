
# studyECS

キューブ400個を自作ParallelJobで並列化
https://github.com/user-attachments/assets/8ad91bad-d12d-4ad2-90ee-314fe5cd6932

Just for my learning; use other ECS libraries for real projects!

## 実行時間テスト記録
```
通常のComponent指向

Debug
GameObjectの作成とコンポーネントの追加: 2741 ミリ秒
コンポーネントの変更にかかった時間: 80 ミリ秒

Release
GameObjectの作成とコンポーネントの追加: 183 ミリ秒
コンポーネントの変更にかかった時間: 10 ミリ秒

作成中のECS(SparseSetBased)

5/16
Debug
エンティティの作成とコンポーネントの追加: 3951 ミリ秒
コンポーネントの変更にかかった時間: 1020 ミリ秒

Release
エンティティの作成とコンポーネントの追加: 136 ミリ秒
コンポーネントの変更にかかった時間: 36 ミリ秒
```
## Thanks

I learned from..

ECS
* [toecs] : ECS in Rust
* [seecs] : simple ECS in C++
* [sparsey] : sparseSet
* [ECS back and forth][ebaf] : ECS
* [shipyard]
* [bevy]

HashMap
* [HashMap] : HopScotchHashMap

Event
* [eventtpp] : EventSystem

[toecs]: https://github.com/toyboot4e/toecs/tree/wip
[seecs]: https://github.com/chrischristakis/seecs/tree/master
[sparsey]: https://github.com/LechintanTudor/sparsey
[ebaf]: https://skypjack.github.io/2019-02-14-ecs-baf-part-1/
[shipyard]: https://github.com/leudz/shipyard
[bevy]: https://github.com/bevyengine/bevy
[HashMap]: https://github.com/taqu/HashMap/tree/master
[eventtpp]: https://github.com/wqking/eventpp
