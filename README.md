# Steel's Semitrailer and Towing (framework)

Semitrailers, dollies and tow hitches for Arma Reforger trucks. The framework contains the scripts, the fifth wheel and tow hitch models and one flatbed semitrailer with an M923A1 tractor.
More trailers, the Soviet set and tow hitches for the vanilla trucks are in **SSTTExpanded**.

This readme is for modders who want their trucks to pull the trailers:

- [A. Adding the framework to an existing truck mod](#a-adding-the-framework-to-an-existing-truck-mod)
  (someone else's mod, or one of yours that is already published and should stay as it is)
- [B. Building it into your own truck mod](#b-building-it-into-your-own-truck-mod)

Both use the same two pieces on the truck: the `SST_SemiTractorComponent` and the fifth wheel model.

---

## How it works, in short

- There is no physics joint between truck and trailer. The tractor keeps the trailer's **kingpin** on its **fifth wheel** and moves the trailer behind it every frame (on every machine, nothing about the pose is sent over the network).
- The **origin of every trailer is its kingpin**. The tractor only needs to know where the kingpin sits on its fifth wheel: that is `m_vFifthWheel`, in the truck's own space.
- Coupling: back the truck under a parked trailer, the jaws lock once the kingpin is on the plate.
  Uncoupling is a user action at the front left of the trailer.
- The fifth wheel model is only the visual part. Coupling, pulling and uncoupling are done by the script with `m_vFifthWheel`, the model needs no colliders of its own.

## What you need

|                                     |                                                                                  |
| ----------------------------------- | -------------------------------------------------------------------------------- |
| Framework addon GUID                | `6ACF29CF396C1940`                                                               |
| Arma Reforger GUID                  | `58D0FB3206B6F859`                                                               |
| Tractor component                   | `SST_SemiTractorComponent`                                                       |
| Fifth wheel prefab                  | `{147669274E520343}Prefabs/Vehicles/Wheeled/Semitrailer/Parts/SST_FifthWheel.et` |
| Tow hitch prefab (rear of trailers) | `{B77BB5BB2F64142D}Prefabs/Vehicles/Wheeled/Semitrailer/Parts/SST_TowHitch.et`   |
| Example tractor                     | `{351C630225F76D93}Prefabs/Vehicles/Wheeled/Semitrailer/SST_M923A1_Tractor.et`   |
| Example trailer                     | `{C174CBB6F089C2EB}Prefabs/Vehicles/Wheeled/Semitrailer/SST_Semitrailer_Base.et` |

### `SST_SemiTractorComponent` settings

| Attribute                    | Default       | What it does                                                                                                                                                                              |
| ---------------------------- | ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `m_vFifthWheel`              | `0 1.37 -2.3` | Kingpin position on the fifth wheel, truck space (x right, y up, z forward). See [Finding m_vFifthWheel](#finding-m_vfifthwheel).                                                         |
| `m_sTrailerPrefab`           | empty         | Trailer that spawns already coupled together with the truck. Empty = the truck spawns alone.                                                                                              |
| `m_fCoupleRadius`            | `0.45`        | How close the kingpin has to be to the plate before the jaws lock [m].                                                                                                                    |
| `m_fCoupleSpeed`             | `1.5`         | Max truck speed for coupling and uncoupling [m/s].                                                                                                                                        |
| `m_fMaxArticulation`         | `70`          | Max angle between truck and trailer [deg].                                                                                                                                                |
| `m_fMaxPitch` / `m_fMaxRoll` | `15` / `6`    | Max pitch and roll of the trailer against the truck [deg].                                                                                                                                |
| `m_fHandCoupleRadius`        | `0`           | Distance for coupling a drawbar trailer (dolly) by hand, 0 = what the action allows [m].                                                                                                  |
| `m_bTowHitch`                | off           | The truck has a tow hitch instead of a fifth wheel: only drawbar trailers (dollies) couple.                                                                                               |
| `m_sHitchContext`            | empty         | Action context that marks the tow hook (vanilla trucks: `tow_hook`). With a fifth wheel as well, the truck takes a dolly on the hook and a semitrailer on the fifth wheel, one at a time. |

---

## A. Adding the framework to an existing truck mod

Use this when the truck mod is not yours, or when it is yours but want to have the framework seperate from the main mod. You make a small extra addon (a "compatability" addon) that sits on top of both mods.

### 1. Create the compatibility addon

1. Workbench launcher → **Create new project**, e.g. `MyTrucks_SST`.
2. Add the dependencies to the project (in the launcher's project settings, or directly in its
   `addon.gproj`):
   - Steel's Semitrailer and Towing (`6ACF29CF396C1940`)
   - the truck mod
   - Arma Reforger (`58D0FB3206B6F859`, normally already there)
3. Load both mods together with your addon, so their prefabs show up in the Resource Browser.

`addon.gproj` then looks like this:

```
GameProject {
 ID "MyTrucks_SST"
 GUID "<your GUID>"
 TITLE "My Trucks - Semitrailers"
 Dependencies {
  "6ACF29CF396C1940" "<GUID of the truck mod>" "58D0FB3206B6F859"
 }
}
```

### 2. Pick how the truck gets the fifth wheel

**Option 1 – a tractor version of the truck (recommended).**
Right click the truck prefab in the Resource Browser → **Inherit**, save it in your addon, e.g.
`Prefabs/Vehicles/Wheeled/MyTruck/MyTruck_Tractor.et`. The original truck stays untouched and the tractor is a separate vehicle in the Game Master. This is how the framework does it with the M923A1 (`SST_M923A1_Tractor.et` inherits the vanilla `M923A1.et`).

**Option 2 – every truck of that type gets it.**
Right click the truck prefab → **Override in "MyTrucks_SST"**. Every one of these trucks in every world then has the fifth wheel. Use this only when the truck really should always be a tractor.
SSTTExpanded uses overrides for the vanilla `M923A1.et` and `Ural4320.et`, but only to give them a tow hitch for dollies (see [Tow hook for dollies](#tow-hook-for-dollies)).

### 3. Add the tractor component

Open your prefab, select the root entity → **Add component** → `SST_SemiTractorComponent`.
Set at least `m_vFifthWheel` (next step). For a plain semitrailer tractor that is all.

### 4. Put the fifth wheel model on the truck

The fifth wheel is a child prefab in a slot of the truck's `SlotManagerComponent`:

1. Find the `SlotManagerComponent` on the truck. Vanilla based trucks already have one; if the truck has none, add it.
2. **Slots → +** → `EntitySlotInfo`, name it `FifthWheel`.
3. `Prefab` = `SST_FifthWheel.et`, then move it with `Offset` until the plate sits on the frame behind the cab.
4. Set `m_vFifthWheel` from that offset (below).

Result, as it is in `SST_M923A1_Tractor.et`:

```
SST_SemiTractorComponent "{...}" {
 m_vFifthWheel 0 1.37 -2.3
}
SlotManagerComponent "{55BCE45E438E4CFF}" {
 Slots {
  EntitySlotInfo FifthWheel {
   Offset 0 0 -0.6
   Prefab "{147669274E520343}Prefabs/Vehicles/Wheeled/Semitrailer/Parts/SST_FifthWheel.et"
  }
 }
}
```

> In a child prefab or an override, keep the **component GUID of the parent** (`{55BCE45E438E4CFF}` above is the vanilla one). Workbench does that for you when you edit the inherited component. A second `SlotManagerComponent` with a new GUID does not work.

### Finding m_vFifthWheel

The stock fifth wheel model has its kingpin point **1.37 m above and 1.7 m behind its own origin**.
So with the slot offset `Offset = (x, y, z)`:

```
m_vFifthWheel = (x, y + 1.37, z - 1.7)
```

Examples from the mods:

| Truck                           | Slot `Offset`   | `m_vFifthWheel` |
| ------------------------------- | --------------- | --------------- |
| M923A1                          | `0 0 -0.6`      | `0 1.37 -2.3`   |
| Ural-4320                       | `0 -0.05 -0.47` | `0 1.32 -2.17`  |
| Dolly (child entity, same rule) | `0 -0.95 -1.5`  | `0 0.42 -3.2`   |

If the truck's frame is higher or lower than on these trucks, change only `y` of the offset and add 1.37 again. The plate should sit about where a real one would: over or slightly ahead of the rear axle(s).

### 5. Optional extras

- **Paint the fifth wheel**: on the slot's prefab, or with a child prefab of `SST_FifthWheel.et`, use a `MaterialAssignClass` on its `MeshObject` with `SourceMaterial "SST_FifthWheelPaint"` and your own material. Example: `SST_FifthWheel_CIVBlue.et` in SSTTExpanded.
- **Spawn with a trailer**: set `m_sTrailerPrefab` to a trailer prefab.
- **Camera**: long trains are easier to drive with the camera further back. The framework tractors use `SCR_VehicleCameraDataComponent` with `m_fHeight 4`, `m_fDist_Min 13`, `m_fDist_Max 21`, `m_fDist_Desired 16`.
- **Game Master filter**: add `TRAIT_STEELS_TRUCKS` to `m_aAuthoredLabels` of the truck's `SCR_EditableVehicleComponent` → `m_UIInfo`, then it shows up under the "Steel's Trucks" trait in Game Master.

### 6. Show the tractor in the Game Master

Child prefabs (option 1) are new vehicles and have to be added to a faction's entity catalog. Make an override of the catalog in your addon and append your prefab, as the framework does in
`Configs/EntityCatalog/US/Vehicles_EntityCatalog_US.conf`:

```
SCR_EntityCatalogMultiList {
 m_aMultiLists + {
  SCR_EntityCatalogMultiListEntry "{60569780DE4EE17A}" {
   m_aEntities {
    SCR_EntityCatalogEntry "{<new GUID>}" {
     m_sEntityPrefab "{<GUID>}Prefabs/Vehicles/Wheeled/MyTruck/MyTruck_Tractor.et"
     m_aEntityDataList {
      SCR_EntityCatalogEditorData "{<new GUID>}" {
      }
     }
    }
   }
  }
 }
}
```

Use the catalog of the faction the truck belongs to (`US`, `USSR`, `FIA`, `CIV`), and the `SCR_EntityCatalogMultiListEntry` GUID of the vehicle list in that catalog. If the truck mod has its own catalog, override that one the same way. Overrides (option 2) need no catalog entry, the truck is already listed.

### 7. Publish

Publish the compatability addon with its dependencies set, so the Workshop pulls in the framework and the truck mod. The truck mod itself does not change.

---

## B. Building it into your own truck mod

When the truck mod is yours and may depend on the framework, put everything straight into your truck prefabs:

1. Add the framework (`6ACF29CF396C1940`) to the dependencies of your truck mod.
2. In the tractor prefab, add `SST_SemiTractorComponent` and the `FifthWheel` slot exactly as in
   [A. steps 3 to 5](#3-add-the-tractor-component).
3. Add the tractor to your faction catalog like your other trucks ([A. step 6](#6-show-the-tractor-in-the-game-master)).

Players then need the framework for your mod. If you want your trucks also work without it, keep your truck mod as it is and ship the tractors as a separate addon (part A) instead.

### Using your own fifth wheel model

You can use your own model instead of `SST_FifthWheel.et`:

- Make a prefab with a `MeshObject` for your model and a `Hierarchy` component (needed to sit in the slot), like `SST_FifthWheel.et`.
- No colliders are needed, the model is only visual.
- Measure where the kingpin goes on your plate (centre of the slot in the jaws, on top of the plate) and set `m_vFifthWheel` to that point in **truck space**. The 1.37 / 1.7 rule above is only valid for the stock model.
- Build LODs for the model as for any other vehicle part.

### Modelling the truck itself

Nothing special is needed. Leave room on the frame behind the cab for the plate and keep the rear of the truck below the trailer's deck: the trailer's frame and cargo colliders do not collide with vehicles, but they still collide with the world.

---

## Tow hook for dollies

A truck can also pull a dolly (and a semitrailer on the dolly) from its tow hook:

- **Only a tow hook, no fifth wheel**: `m_bTowHitch 1`, `m_sHitchContext "tow_hook"`,
  `m_fHandCoupleRadius 3`. The hitch point is read from the truck's `tow_hook` action context, which vanilla trucks have. This is what SSTTExpanded adds to the vanilla M923A1 and Ural-4320:

  ```
  Vehicle {
   ID "<ID of the vanilla prefab>"
   components {
    SST_SemiTractorComponent "{2806236C42B569F0}" {
     m_bTowHitch 1
     m_fHandCoupleRadius 3
     m_sHitchContext "tow_hook"
    }
   }
  }
  ```

- **Fifth wheel and tow hook**: leave `m_bTowHitch` off and set `m_sHitchContext`. A semitrailer goes on the fifth wheel, a dolly on the hook, one at a time.

If your truck has no `tow_hook` context, add a `UserActionContext` with that name to its `ActionsManagerComponent` at the hook. Dollies and the trailers with a rear tow hitch are in SSTTExpanded.

---

## Testing

1. Place the tractor and a trailer (Game Master → Systems → Steel's Trucks).
2. Drive the tractor backwards under the front of the trailer. The jaws lock when the kingpin is on the plate (slower than 1.5 m/s).
3. Check from the side that the trailer sits level on the plate. If it hangs too high or too low, correct `y` of `m_vFifthWheel`; if it couples too far forward or back, correct `z`.
4. Drive a full turn: trailer and cab must not cut into each other. If they do, move the fifth wheel back a little (smaller `z`) or lower `m_fMaxArticulation`.
5. Uncouple at the front left of the trailer and drive away. The trailer stays on its legs.

## Troubleshooting

| Problem                                      | Cause                                                                                                     |
| -------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| Truck never couples                          | `SST_SemiTractorComponent` missing, or `m_bTowHitch` is on (then only dollies couple).                    |
| Trailer floats above or sinks into the truck | `m_vFifthWheel` `y` wrong.                                                                                |
| Fifth wheel model not visible                | Slot added to a new `SlotManagerComponent` instead of the truck's own one, or the prefab path is wrong.   |
| Tractor missing in the Game Master           | Not in an entity catalog (child prefabs need an entry), or the catalog override uses the wrong list GUID. |
| "Missing addon" when loading                 | The compat addon's dependencies are not loaded: framework and truck mod must be loaded too.               |
