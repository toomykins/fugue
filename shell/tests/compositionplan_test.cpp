#include <QtTest>
#include "tests/testmain.h"
#include "shell/compositionplan.h"
#include "shell/mapinput.h"
#include "shell/tests/planesourcefixture.h"
#include "shell/tests/turflocalfixture.h"
#include "shell/tests/compositionfixture.h"
using namespace fugue::shell;
using test::ink;
using test::master;
using test::relay;
using test::contributorScene;
namespace {
const QRect viewport(0,0,32,32);
std::optional<MapPointer> pick(const std::shared_ptr<const CompositionPlan>& plan, QPoint point = {8,8}) {
    return mapPointerAtPresentation(point,viewport,{1,1},viewport,
        {{0,0,1,1},{1,1},32,0,1},"map",true,{},plan);
}
}
class CompositionPlanTest : public QObject {
    Q_OBJECT
private slots:
    void contributorMaterialBounds() {
        const QRect source(13,-9,832,704);
        auto scene=contributorScene(source,1024);
        const auto old=CompositionPlan::build(scene,source,{true,false});
        const auto bounded=CompositionPlan::build(scene,source,{true,true});
        QVERIFY(old->supported && bounded->supported);
        QCOMPARE(old->schedule,bounded->schedule);QCOMPARE(old->livePasses,uint64_t(3072));
        int tightened=0;
        for (size_t i=0;i<old->passes.size();++i) {
            const auto& a=old->passes[i];const auto& b=bounded->passes[i];
            QCOMPARE(a.byteCapture,b.byteCapture);QCOMPARE(a.straightOutput,b.straightOutput);
            QCOMPARE(a.items.size(),b.items.size());
            if (a.straightOutput) {
                const auto clipped=b.items[0].sprite->rect().intersected(source);
                QCOMPARE(b.bounds,clipped.isEmpty() ? source : clipped);
                tightened+=a.bounds!=b.bounds;
            } else QCOMPARE(a.bounds,b.bounds); // every accumulated/rounded surface unchanged
            for (size_t j=0;j<a.items.size();++j) {
                QCOMPARE(a.items[j].input,b.items[j].input);QCOMPARE(a.items[j].effect,b.items[j].effect);
                QCOMPARE(a.items[j].sprite->blend,b.items[j].sprite->blend);
            }
        }
        QCOMPARE(tightened,1003);
        QVERIFY(bounded->storage.workPixels < old->storage.workPixels*7/10);
        qInfo()<<"logical weighted work pixels reference/bounded"<<old->storage.workPixels<<bounded->storage.workPixels;
        // No writer aliases a still-live reader, including final outputs.
        for (const auto& plan:{old,bounded}) {
            std::map<int,int> owners;
            for (int index:plan->schedule) {
                const auto& pass=plan->passes[index];
                for (const auto& item:pass.items) if (item.input>=0) {
                    const int slot=plan->passes[item.input].surface;
                    QCOMPARE(owners.at(slot),item.input);QVERIFY(slot!=pass.surface);
                }
                owners[pass.surface]=index;
            }
            for (const auto& item:plan->final) if (item.input>=0)
                QCOMPARE(owners.at(plan->passes[item.input].surface),item.input);
        }
        const auto grouped=contributorScene(source,73,7);
        const auto oldGroup=CompositionPlan::build(grouped,source,{true,false});
        const auto newGroup=CompositionPlan::build(grouped,source,{true,true});
        QVERIFY(oldGroup->supported && newGroup->supported);int generatedTightened=0;
        for (size_t i=0;i<newGroup->passes.size();++i) {
            const auto& p=newGroup->passes[i];
            if (p.straightOutput && p.items[0].input>=0 && p.bounds!=oldGroup->passes[i].bounds)
                ++generatedTightened;
        }
        QVERIFY(generatedTightened>0);
        const auto byteOnly=CompositionPlan::build(scene,source,{false,true});
        QVERIFY(byteOnly->supported);
        for (const auto& p:byteOnly->passes) if(p.straightOutput) QCOMPARE(p.bounds,source);
        scene.clear();QCOMPARE(bounded->sprites.size(),qsizetype(1025));
        // Named sources resolve later; affines need their half-texel support.
        auto s=ink(1);s.tint[3]=.5f;s.renderSource="*later";
        auto plan=CompositionPlan::build({s,master(1,"visible")},source,{true,true});
        for (const auto& p:plan->passes) if(p.straightOutput) QCOMPARE(p.bounds,source);
        s.renderSource.clear();s.affine=StaticAffine{};
        plan=CompositionPlan::build({s,master(1,"visible")},source,{true,true});
        for (const auto& p:plan->passes) if(p.straightOutput) QCOMPARE(p.bounds,source);
    }
    void anonymousGroupsAreOccurrenceScopedAndBounded() {
        auto group=std::make_shared<AppearanceGroup>();group->tint[3]=.5f;
        auto a=ink();a.appearanceGroup=group;a.groupOccurrence=1;
        auto b=a;b.position={16,0};b.groupOccurrence=2;
        auto plan=CompositionPlan::build({a,b},{0,0,64,32},{true});QVERIFY(plan->supported);
        // A group of one icon has no surface: the icon is drawn with the group's material (stock 516.1689,
        // stock-parity-world ta-kt-alone-turn).
        QCOMPARE(plan->final.size(),size_t(2));QCOMPARE(plan->final[0].input,-1);QCOMPARE(plan->final[1].input,-1);
        QCOMPARE(plan->final[0].sprite->tint[3],.5f);QCOMPARE(plan->final[0].original,qsizetype(0));
        auto a2=a;a2.position={4,0};auto b2=b;b2.position={20,0};
        plan=CompositionPlan::build({a,a2,b,b2},{0,0,64,32},{true});QVERIFY(plan->supported);
        QCOMPARE(plan->final.size(),size_t(2));QVERIFY(plan->final[0].input>=0 && plan->final[0].input!=plan->final[1].input);
        // Untransformed group parts keep their own ink (stock 516.1688, tests/data/click-world kt-root).
        QVERIFY(!plan->bindings[0].refused && !plan->bindings[1].refused);
        b.groupOccurrence=1;plan=CompositionPlan::build({a,b},{0,0,64,32},{true});
        QVERIFY(plan->supported);QCOMPARE(plan->final.size(),size_t(1));
        // A turned group's surface is the box of its parts' ink and ends there (surface-bounds): two 8 px boxes 16 px apart.
        auto turned=std::make_shared<AppearanceGroup>();turned->transform=QTransform().rotate(45);
        auto c=ink();c.image.fill(Qt::transparent);
        for (int y=4;y<12;++y) for (int x=4;x<12;++x) c.image.setPixelColor(x,y,Qt::red);
        c.appearanceGroup=turned;c.groupOccurrence=3;auto d=c;d.position={16,0};
        const auto hard=CompositionPlan::build({c,d},{0,0,64,32},{true});QVERIFY(hard->supported);
        QCOMPARE(hard->final.size(),size_t(1));QVERIFY(hard->final[0].hardEdge);
        QCOMPARE(QRect(hard->final[0].sprite->position,hard->final[0].sprite->size),QRect(4,4,24,8));
        // The frame owns its descriptor even when the caller drops its references.
        a.appearanceGroup.reset();b.appearanceGroup.reset();group.reset();
        QCOMPARE(plan->sprites[0].appearanceGroup->tint[3],.5f);
        std::shared_ptr<const AppearanceGroup> nested;
        for (int i=0;i<33;++i) {auto node=std::make_shared<AppearanceGroup>();node->parent=nested;nested=node;}
        a.appearanceGroup=nested;QVERIFY(!CompositionPlan::build({a},viewport,{true})->supported);
        auto invalid=std::make_shared<AppearanceGroup>();invalid->color[0]=std::numeric_limits<float>::infinity();
        a.appearanceGroup=invalid;QVERIFY(!CompositionPlan::build({a},viewport,{true})->supported);
    }
    void planeMaskDependenciesAndRefusals() {
        auto mask=ink(3);mask.image.fill(QColor(0,0,0,128));
        auto output=master(1);auto effects=std::make_shared<PlaneEffects>();
        PlaneFilter filter;filter.kind=PlaneFilter::AlphaMask;filter.source="*mask";
        effects->filters={filter};output.planeEffects=effects;
        MapSprites sprites{ink(1),output,mask,master(3,"*mask")};
        auto build=[&]{return CompositionPlan::build(sprites,viewport,{true});};
        auto plan=build();QVERIFY(plan->supported);
        int maskPass=-1,dependency=-1;
        for(size_t p=0;p<plan->passes.size();++p) for(const auto& item:plan->passes[p].items)
            if(item.maskSource) {maskPass=int(p);dependency=item.input;}
        QVERIFY(maskPass>=0 && dependency>=0);QCOMPARE(dependency,plan->targets.at("*mask"));
        QVERIFY(std::find(plan->schedule.begin(),plan->schedule.end(),dependency) <
            std::find(plan->schedule.begin(),plan->schedule.end(),maskPass));
        auto padded=std::make_shared<PlaneEffects>();PlaneFilter blur;blur.size=1;
        padded->filters={blur};sprites[3].planeEffects=padded;
        plan=build();QVERIFY(plan->supported);
        const auto bounds=plan->passes[plan->targets.at("*mask")].bounds;QVERIFY(bounds!=viewport);
        for(const auto& pass:plan->passes) for(const auto& item:pass.items) if(item.maskSource)
            QCOMPARE(item.sprite->rect(),bounds);
        sprites[3].planeEffects.reset();
        // Missing/ambiguous masks use a fresh transparent source, never the
        // previous frame's texture. Inversion is material, not source selection.
        for(bool duplicate:{false,true}) {
            auto changed=sprites;
            if(duplicate) changed.push_back(master(4,"*mask"));else changed.removeLast();
            plan=CompositionPlan::build(changed,viewport,{true});QVERIFY(plan->supported);
            for(const auto& pass:plan->passes) for(const auto& item:pass.items) if(item.maskSource) {
                QVERIFY(item.input>=0);QVERIFY(plan->passes[item.input].items.empty());
            }
        }
        // A self-dependent capture cuts its mask edge deterministically.
        sprites[1].renderTarget="*self";effects->filters[0].source="*self";
        sprites.push_back(relay(5,"*self"));plan=build();QVERIFY(plan->supported);
        bool cut=false;
        for(const auto& pass:plan->passes) for(const auto& item:pass.items) if(item.maskSource && item.cyclicInput>=0) {
            cut=true;QVERIFY(item.input>=0);QVERIFY(plan->passes[item.input].items.empty());
            QVERIFY(std::find(plan->schedule.begin(),plan->schedule.end(),item.input)!=plan->schedule.end());
        }
        QVERIFY(cut);
        QCOMPARE(CompositionPlan::build(sprites,viewport,{false})->masterDisposition.at(1),
            CompositionPlan::MasterDisposition::Refused);
        // The master's own colour applies after the mask (stock-parity-world mask-color).
        sprites[1].tint[0]=.5f;
        QCOMPARE(build()->masterDisposition.at(1),CompositionPlan::MasterDisposition::Captured);
    }
    void sharedBindingsAndOwnership() {
        test::PlaneSourceFixture f;
        f.relay({{"filters",test::PlaneSourceFixture::blur("0000803f")}});
        auto second = f.sprites.back(); second.atom = 202; second.plane = 71;
        f.sprites.push_back(second);
        const auto plan = CompositionPlan::build(f.sprites,f.frame.source,{true});
        QVERIFY(plan->supported);
        std::vector<qsizetype> consumers;
        for (qsizetype i = 0; i < plan->sprites.size(); ++i)
            if (!plan->sprites[i].renderSource.isEmpty()) consumers.push_back(i);
        QCOMPARE(consumers.size(),size_t(2));
        const auto& a = plan->bindings[size_t(consumers[0])];
        const auto& b = plan->bindings[size_t(consumers[1])];
        QVERIFY(a.planeSource >= 0); QVERIFY(!a.refused && !b.refused);
        QCOMPARE(a.planeSource,b.planeSource); QCOMPARE(a.sourceOperation,b.sourceOperation);
        QVERIFY(a.sourceOperation >= 0);
        QCOMPARE(plan->final[plan->final.size()-2].input,a.sourceOperation);
        QCOMPARE(plan->final.back().input,b.sourceOperation);
        const auto selected = plan->planeSources.surfaces[a.planeSource].master;
        QCOMPARE(selected,plan->masters.at(60));
        // Pending worker values must not be substituted for the supplied frame.
        f.sprites.clear();
        const auto hit = mapPointerAtPresentation({69,37},f.frame.source,{5,3},f.frame.source,
            f.frame.geometry,"map",true,f.sprites,plan);
        QVERIFY(hit && hit->relayHit); QCOMPARE(hit->composition,plan);
        QCOMPARE(hit->click.over.ref,202u);
        QVERIFY(!hit->relayHit->producer.composition && !hit->relayHit->ink->composition);
        QCOMPARE(hit->relayHit->producer.click.over.ref,plan->sprites[selected].atom);
        const auto hole = mapPointerAtPresentation({79,47},f.frame.source,{5,3},f.frame.source,
            f.frame.geometry,"map",true,{},plan);
        QVERIFY(hole && !hole->relayHit); // generated blur ink is not coverage
    }
    void selectedMastersAndDuplicateNames() {
        auto refused = master(1,"*source");
        auto effects = std::make_shared<PlaneEffects>(); effects->supported = false; refused.planeEffects = effects;
        const auto valid = master(1,"*source");
        MapSprites scene{ink(1),refused,valid,relay(2,"*source")};
        auto plan = CompositionPlan::build(scene,viewport,{true});
        QVERIFY(plan->supported); QCOMPARE(plan->masters.at(1),qsizetype(2));
        QCOMPARE(plan->bindings[3].sourceOperation,plan->targets.at("*source"));
        QVERIFY(!plan->bindings[3].refused);
        scene[2] = refused;
        const auto replaced = CompositionPlan::build(scene,viewport,{true});
        QVERIFY(replaced->supported); QVERIFY(replaced->bindings[3].refused);
        QCOMPARE(replaced->masterDisposition.at(1),CompositionPlan::MasterDisposition::Refused);
        QCOMPARE(replaced->final.back().input,-2);
        QVERIFY(!plan->bindings[3].refused); // earlier plan owns its immutable scene
        scene[2] = valid; scene.push_back(master(3,"*source"));
        const auto duplicate = CompositionPlan::build(scene,viewport,{true});
        QVERIFY(duplicate->supported); QVERIFY(duplicate->bindings[3].refused);
        QCOMPARE(duplicate->bindings[3].planeSource,-1);
        QCOMPARE(duplicate->planeTargetCounts.at("*source"),2);
        QCOMPARE(duplicate->final.back().input,-2);
    }
    void localBindingsDuplicatesAndCycles() {
        auto part = ink(); part.localGroup = 1; part.renderTarget = "*local";
        auto consumer = relay(1,"*local"); consumer.position = {4,0};
        MapSprites scene{part,consumer};
        const auto plan = CompositionPlan::build(scene,viewport,{true});
        QVERIFY(plan->supported); QCOMPARE(plan->bindings[1].localSource,0);
        QCOMPARE(plan->bindings[1].sourceOperation,plan->targets.at("*local"));
        QCOMPARE(plan->passes[plan->targets.at("*local")].bounds,viewport);
        QCOMPARE(plan->final.back().sprite->rect(),viewport.translated(4,0));
        const auto hit = pick(plan); QVERIFY(hit && hit->relayHit);
        QCOMPARE(hit->composition,plan); QCOMPARE(hit->click.over.ref,43u);
        QCOMPARE(mapPointerStack(*hit).size(),1);
        auto other = part; other.localGroup = 2; scene.push_back(other);
        const auto duplicate = CompositionPlan::build(scene,viewport,{true});
        // Two producers of one name: stock relays one of them (stock-parity-world shared-render-target), here the first.
        QVERIFY(duplicate->supported); QVERIFY(!duplicate->bindings[1].refused);
        QCOMPARE(duplicate->bindings[1].localSource,0);
        part.renderSource = "*other"; other.renderTarget = "*other"; other.renderSource = "*local";
        const auto cycle = CompositionPlan::build({part,other,consumer},viewport,{true});
        QVERIFY(cycle->supported); QVERIFY(cycle->bindings[2].refused);
        QVERIFY(!cycle->local.groups[0].valid && !cycle->local.groups[1].valid);
        QVERIFY(cycle->local.groups[0].cyclic);
        QVERIFY(cycle->schedule.empty());
    }
    void planeCycleEdgesAndSchedule() {
        const auto plan = CompositionPlan::build({relay(1,"*b"),master(1,"*a"),
            relay(2,"*a"),master(2,"*b"),relay(3,"*a")},viewport,{true});
        QVERIFY(plan->supported);
        int cycles = 0;
        std::set<int> executed;
        for (int index : plan->schedule) {
            const auto& pass = plan->passes[index];
            QCOMPARE(pass.bounds,viewport); QVERIFY(pass.surface >= 0);
            for (const auto& item : pass.items) {
                if (item.input >= 0) QVERIFY(executed.count(item.input));
                if (item.cyclicInput >= 0) {
                    ++cycles; QCOMPARE(item.input,-2); QVERIFY(item.original >= 0);
                    QVERIFY(plan->bindings[size_t(item.original)].refused);
                    QCOMPARE(plan->bindings[size_t(item.original)].sourceOperation,-2);
                }
            }
            executed.insert(index);
        }
        QCOMPARE(cycles,1); QCOMPARE(plan->schedule.size(),size_t(plan->livePasses));
        QVERIFY(!pick(plan)->relayHit); // chained original-ink ownership remains unsupported
    }
    void backendAdmissionBoundsAndOmissions() {
        auto m = master(1); auto effects = std::make_shared<PlaneEffects>();
        PlaneFilter outline; outline.kind = PlaneFilter::Outline; outline.size = 2;
        effects->filters = {outline}; m.planeEffects = effects;
        const auto full = CompositionPlan::build({ink(1),m},viewport,{true});
        QVERIFY(full->supported); QVERIFY(!full->hiddenTree.count(1));
        QCOMPARE(pick(full)->click.over.ref,42u);
        QVERIFY(std::any_of(full->passes.begin(),full->passes.end(),[](const auto& pass) {
            return pass.bounds.width() > viewport.width();
        }));
        const auto limited = CompositionPlan::build({ink(1),m},viewport,{false});
        QVERIFY(limited->supported); QVERIFY(limited->hiddenTree.count(1));
        QCOMPARE(limited->masterDisposition.at(1),CompositionPlan::MasterDisposition::Refused);
        QVERIFY(limited->final.empty()); QVERIFY(mapPointerStack(*pick(limited)).empty());
        QVERIFY(pick(limited)->click.over.ref != 42u);
        // Omitted filters remain omission metadata, not a new refusal or effect.
        effects = std::make_shared<PlaneEffects>(); effects->ignoredFilters = 3; m.planeEffects = effects;
        const auto omitted = CompositionPlan::build({ink(1),m},viewport,{false});
        QVERIFY(omitted->supported); QCOMPARE(omitted->ignoredFilters,uint64_t(3));
        QVERIFY(omitted->passes.empty()); QCOMPARE(pick(omitted)->click.over.ref,42u);
        test::PlaneSourceFixture f;
        f.relay({{"filters",test::PlaneSourceFixture::blur("0000803f")}});
        const auto relayPlan = CompositionPlan::build(f.sprites,f.frame.source,{false});
        QVERIFY(relayPlan->supported);
        for (qsizetype i = 0; i < f.sprites.size(); ++i)
            if (!f.sprites[i].planeSourceDependency.isEmpty()) QVERIFY(relayPlan->bindings[size_t(i)].refused);
        const auto hit = mapPointerAtPresentation({69,37},f.frame.source,{5,3},f.frame.source,
            f.frame.geometry,"map",true,{},relayPlan);
        QVERIFY(hit && !hit->relayHit);
    }
    void surfaceRejectionDiagnostics() {
        QCOMPARE(planSurfaces({}, {}, 2).rejection.reason,PlanRejection::EmptySource);
        QCOMPARE(planSurfaces({1,1}, {}, 3).rejection.reason,PlanRejection::InvalidPixelWeight);
        for (const SurfaceLifetime bad : {SurfaceLifetime{{},0}, {{1,1},-1}, {{1,1},2}}) {
            const auto plan=planSurfaces({1,1},std::array{bad},2);
            QVERIFY(!plan.supported); QCOMPARE(plan.rejection.reason,PlanRejection::InvalidPass);
            QCOMPARE(plan.rejection.pass,0);
        }
        // Numeric overflow remains invalid; these are planner-only sizes, never allocated.
        const int huge=std::numeric_limits<int>::max();
        const auto overflow=planSurfaces({1,1},std::array<SurfaceLifetime,3>{{
            {{huge,huge},1},{{huge,huge},2},{{huge,huge},3}}},2);
        QVERIFY(!overflow.supported); QCOMPARE(overflow.rejection.reason,PlanRejection::ArithmeticOverflow);
        const auto recovered=planSurfaces({32,32},{},2);
        QVERIFY(recovered.supported); QCOMPARE(recovered.rejection.reason,PlanRejection::None);
    }
    void warningOnlyPlanning() {
        MapSprites scene;
        for(int i=0;i<2200;++i) scene.push_back(ink(1));
        scene.push_back(master(1,"*source"));
        const auto hidden=CompositionPlan::build(scene,viewport,{true});
        QVERIFY(hidden->supported); QCOMPARE(hidden->metrics.generated,uint64_t(4400));
        QCOMPARE(hidden->metrics.passes,uint64_t(0)); QCOMPARE(hidden->metrics.surfaces,uint64_t(1));
        scene.push_back(relay(2,"*source"));
        const auto live=CompositionPlan::build(scene,viewport,{true});
        QVERIFY(live->supported); QVERIFY(pick(live));
        QCOMPARE(live->metrics.generated,uint64_t(4400)); QCOMPARE(live->metrics.passes,uint64_t(4400));
        QCOMPARE(live->schedule.size(),size_t(4400)); // deep iterative DFS, no old cap/truncation
        QVERIFY(live->metrics.surfaces < 10);
        const auto large=CompositionPlan::build({}, {0,0,65536,32768}, {true});
        QVERIFY(large->supported);
        QCOMPARE(large->metrics.workPixels,uint64_t(2147483648ull));
        QCOMPARE(large->metrics.storagePixels,large->metrics.workPixels);
        MapSprites pinned;
        for(int i=0;i<64;++i) {pinned.push_back(ink(i));pinned.push_back(master(i,QString::number(i)));}
        const auto storage=CompositionPlan::build(pinned,viewport,{true});
        QVERIFY(storage->supported); QVERIFY(storage->metrics.surfaces > CompositionMetrics::surfaceCap);
        const auto normal=CompositionPlan::build({ink()},viewport,{true});
        QVERIFY(normal->supported); QCOMPARE(normal->metrics.generated,uint64_t(0));
        QCOMPARE(live->metrics.generated,uint64_t(4400)); // immutable prior metrics
        const auto empty=CompositionPlan::build({}, {}, {true});
        QVERIFY(!empty->supported); QVERIFY(!empty->metrics.available);
    }
    void warningPresentation() {
        CompositionMetrics m;
        QVERIFY(m.html().contains("-- / 4096")); QVERIFY(!m.html().contains("#ffbf69"));
        const auto members=std::array{&CompositionMetrics::generated,&CompositionMetrics::passes,
            &CompositionMetrics::surfaces,&CompositionMetrics::workPixels,&CompositionMetrics::storagePixels};
        for(size_t i=0;i<members.size();++i) {
            const auto cap=m.rows()[i].cap;
            for(int delta : {-1,0,1}) {
                m={};m.available=true;m.*members[i]=cap+delta;
                QCOMPARE(m.html().count("#ffbf69"),delta>0 ? 1 : 0);
                QVERIFY(m.html().contains(QString::number(cap+delta)+" / "+QString::number(cap)));
            }
        }
        m={};m.available=true; QVERIFY(!m.html().contains("#ffbf69"));
    }

};
FUGUE_TEST_MAIN(CompositionPlanTest)
#include "compositionplan_test.moc"
