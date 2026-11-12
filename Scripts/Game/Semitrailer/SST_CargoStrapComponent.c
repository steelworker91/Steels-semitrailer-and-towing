// Straps for the load on a trailer deck.
//
// A load that is not strapped down comes off at the first hard jolt (full braking, a fast turn) or
// when the trailer leans over too far. Strapped loads stay on, but every hard braking or fast turn
// costs the straps a bit: the driver gets a warning when they work loose, a second one shortly before
// they break, and if nobody tightens them in the meantime they break and the load comes off.
//
// Which loads ride the deck and how they come off is up to the trailer (GetStrapLoads, DropStrapLoad
// and GetStrapBounds of SST_SemiTrailerComponent, filled in by the add-ons: vehicles on a carrier,
// C-130 loads on the flat trailer).
//
// The server measures the trailer and keeps the wear. Clients only get which loads are strapped and
// how loose the straps are (0 tight, 1 loose, 2 about to break, for the warnings) and draw the straps
// themselves, as local children of the trailer. The straps are only built again when other loads are
// strapped, not while driving.

[ComponentEditorProps(category: "SST/Semitrailer", description: "Straps for the load of a trailer")]
class SST_CargoStrapComponentClass : ScriptComponentClass
{
}

class SST_CargoStrapComponent : ScriptComponent
{
	[Attribute("{5E0BD13F731DE7E8}Prefabs/Vehicles/Wheeled/Semitrailer/Parts/SST_CargoStrap.et", UIWidgets.ResourceNamePicker, "Strap piece, 1 m long along +Z from its origin", params: "et")]
	protected ResourceName m_sStrapPrefab;

	[Attribute("3", UIWidgets.EditBox, "Straps wear above this acceleration of the trailer [m/s2]", category: "Wear")]
	protected float m_fWearAccel;

	[Attribute("60", UIWidgets.EditBox, "1 m/s2 above the limit for this long breaks the straps [s]", category: "Wear")]
	protected float m_fWearTime;

	[Attribute("4", UIWidgets.EditBox, "A load that is not strapped comes off above this acceleration [m/s2]", category: "Unsecured")]
	protected float m_fLooseAccel;

	[Attribute("20", UIWidgets.EditBox, "A load that is not strapped comes off when the trailer leans more than this [deg]", category: "Unsecured")]
	protected float m_fLooseTilt;

	[Attribute("1", UIWidgets.EditBox, "Max speed of the train for putting on and releasing the straps [m/s]")]
	protected float m_fMaxSpeed;

	[Attribute("1.37", UIWidgets.EditBox, "Half width of the deck, the straps are tied at its edges [m]")]
	protected float m_fDeckHalfWidth;

	[Attribute("0.45", UIWidgets.EditBox, "Height of the deck edge the straps are tied to, trailer space [m]")]
	protected float m_fDeckY;

	[Attribute("0.8", UIWidgets.EditBox, "Lashing rings: z of the front pair, trailer space [m]", category: "Rings")]
	protected float m_fRingFront;

	[Attribute("1.5857", UIWidgets.EditBox, "Lashing rings: distance from one pair to the next towards the rear [m]", category: "Rings")]
	protected float m_fRingSpacing;

	[Attribute("8", UIWidgets.EditBox, "Lashing rings: pairs along the deck. 0: the straps end at the deck edge", category: "Rings")]
	protected int m_iRingCount;

	[Attribute("1.33", UIWidgets.EditBox, "Lashing rings: distance from the middle of the trailer [m]", category: "Rings")]
	protected float m_fRingX;

	[Attribute("0.53", UIWidgets.EditBox, "Lashing rings: height, trailer space [m]", category: "Rings")]
	protected float m_fRingY;

	[Attribute("1.6", UIWidgets.EditBox, "Max distance between two straps along the load [m]")]
	protected float m_fStrapSpacing;

	// loads that are strapped and how loose their straps are
	[RplProp(onRplName: "OnStrapsChanged")]
	protected ref array<RplId> m_aIds = {};

	[RplProp(onRplName: "OnStrapsChanged")]
	protected ref array<int> m_aStage = {};

	// how each load is strapped (STRAP_NORMAL, STRAP_HIDDEN, STRAP_FIXED)
	[RplProp(onRplName: "OnStrapsChanged")]
	protected ref array<int> m_aMode = {};

	// server
	protected ref array<float> m_aWear = {};
	protected SST_SemiTrailerComponent m_Trailer;
	protected int m_iMeasure;
	protected vector m_vLastPos;
	protected vector m_vVel;
	protected vector m_vAccel;
	protected float m_fSettle;
	protected float m_fCleanupTimer;

	// strap pieces, every machine but a dedicated server
	protected bool m_bDraw;
	protected ref Resource m_StrapRes;
	protected ref array<IEntity> m_aPieces = {};
	protected string m_sDrawn;
	protected ref array<RplId> m_aDrawnIds = {};
	protected ref array<int> m_aDrawnModes = {};
	protected float m_fDrawTimer;

	// hints already shown, so a client only warns once per stage
	protected ref array<RplId> m_aHintIds = {};
	protected ref array<int> m_aHintStage = {};

	//! Straps that wear and can be seen
	static const int STRAP_NORMAL = 0;
	//! Nothing is drawn (many loads, weak machines), never work loose or break
	static const int STRAP_HIDDEN = 1;
	//! Drawn, never work loose or break
	static const int STRAP_FIXED = 2;

	protected static const float LOOSE_AT = 0.5;
	protected static const float BREAK_SOON_AT = 0.8;
	protected static const float VEL_SMOOTH = 0.15;
	protected static const float ACCEL_SMOOTH = 0.3;
	protected static const float SETTLE_TIME = 0.6;
	protected static const float TELEPORT_SPEED = 100;
	protected static const float HINT_TIME = 8;
	protected static const float STRAP_CLEARANCE = 0.015;
	protected static const float WHEEL_MARGIN = 0.06;
	protected static const float MIN_STRAP_GAP = 0.4;
	protected static const float WHEEL_TIE_OUT = 0.3;
	protected static const float WHEEL_TIE_AHEAD = 0.3;
	protected static const int WHEEL_ARC_PIECES = 8;
	protected static const int BLADE_SAMPLES = 180;
	protected static const int ROTOR_STRAPS = 4;
	protected static const float PROFILE_STEP = 0.05;

	protected static IEntity s_TraceLoad;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame() || !GetGame().InPlayMode())
			return;

		m_Trailer = SST_SemiTrailerComponent.Cast(owner.FindComponent(SST_SemiTrailerComponent));
		m_bDraw = RplSession.Mode() != RplMode.Dedicated;
		SetEventMask(owner, EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		DeletePieces();
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	static SST_CargoStrapComponent Get(IEntity trailer)
	{
		if (!trailer)
			return null;

		return SST_CargoStrapComponent.Cast(trailer.FindComponent(SST_CargoStrapComponent));
	}

	//------------------------------------------------------------------------------------------------
	//! Is this load strapped down on the trailer it rides on. Unloading has to wait for that.
	static bool IsStrapped(IEntity load)
	{
		if (!load)
			return false;

		SST_CargoStrapComponent straps = Get(load.GetParent());
		return straps && straps.IndexOf(load) >= 0;
	}

	//------------------------------------------------------------------------------------------------
	protected int IndexOf(IEntity load)
	{
		RplComponent rpl = RplComponent.Cast(load.FindComponent(RplComponent));
		if (!rpl)
			return -1;

		return m_aIds.Find(rpl.Id());
	}

	//------------------------------------------------------------------------------------------------
	protected static IEntity FindEntity(RplId id)
	{
		if (!id.IsValid())
			return null;

		RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
		if (!rpl)
			return null;

		return rpl.GetEntity();
	}

	//------------------------------------------------------------------------------------------------
	//! Loads on the deck, how many of them are strapped and the loosest straps (0..2)
	void GetStatus(out int loads, out int secured, out int stage)
	{
		int inMode;
		GetModeStatus(-1, loads, secured, inMode, stage);
	}

	//------------------------------------------------------------------------------------------------
	//! Same, plus how many loads are strapped the way mode says (-1: any way)
	void GetModeStatus(int mode, out int loads, out int secured, out int inMode, out int stage)
	{
		loads = 0;
		secured = 0;
		inMode = 0;
		stage = 0;
		if (!m_Trailer)
			return;

		array<IEntity> onDeck = {};
		m_Trailer.GetStrapLoads(onDeck);
		loads = onDeck.Count();
		foreach (IEntity load : onDeck)
		{
			int i = IndexOf(load);
			if (i < 0)
				continue;

			secured++;
			if (i < m_aStage.Count())
				stage = Math.Max(stage, m_aStage[i]);

			if (mode < 0 || GetMode(i) == mode)
				inMode++;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Straps go on and off only while the train stands
	bool IsStationary()
	{
		if (!m_Trailer || !m_Trailer.GetTractor())
			return true;

		Physics phys = SST_SemiTractorComponent.GetChainHead(GetOwner()).GetPhysics();
		return !phys || phys.GetVelocity().Length() <= m_fMaxSpeed;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: strap down everything on the deck, straps that are already on are tightened again
	void SecureAll(int mode = STRAP_NORMAL)
	{
		if (!Replication.IsServer() || !m_Trailer)
			return;

		array<IEntity> loads = {};
		m_Trailer.GetStrapLoads(loads);

		m_aIds.Clear();
		m_aStage.Clear();
		m_aMode.Clear();
		m_aWear.Clear();
		foreach (IEntity load : loads)
		{
			RplComponent rpl = RplComponent.Cast(load.FindComponent(RplComponent));
			if (!rpl)
				continue;

			m_aIds.Insert(rpl.Id());
			m_aStage.Insert(0);
			m_aMode.Insert(mode);
			m_aWear.Insert(0);
		}

		Replication.BumpMe();
		OnStrapsChanged();
	}

	//------------------------------------------------------------------------------------------------
	//! Server
	void ReleaseAll()
	{
		if (!Replication.IsServer() || m_aIds.IsEmpty())
			return;

		m_aIds.Clear();
		m_aStage.Clear();
		m_aMode.Clear();
		m_aWear.Clear();
		Replication.BumpMe();
		OnStrapsChanged();
	}

	//------------------------------------------------------------------------------------------------
	protected int GetMode(int i)
	{
		if (i < m_aMode.Count())
			return m_aMode[i];

		return STRAP_NORMAL;
	}

	//------------------------------------------------------------------------------------------------
	protected void RemoveAt(int i)
	{
		m_aIds.RemoveOrdered(i);
		if (i < m_aStage.Count())
			m_aStage.RemoveOrdered(i);

		if (i < m_aMode.Count())
			m_aMode.RemoveOrdered(i);

		if (i < m_aWear.Count())
			m_aWear.RemoveOrdered(i);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (Replication.IsServer())
			UpdateWear(owner, timeSlice);

		if (!m_bDraw)
			return;

		m_fDrawTimer += timeSlice;
		if (m_fDrawTimer > 0.5)
		{
			m_fDrawTimer = 0;
			UpdatePieces(owner, false);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server: what the load feels is the acceleration of the trailer. Measured from where the
	//! trailer is, the trailer has no moving physics of its own while it is towed.
	protected void UpdateWear(IEntity owner, float timeSlice)
	{
		m_fCleanupTimer += timeSlice;
		if (m_fCleanupTimer > 1)
		{
			m_fCleanupTimer = 0;
			Cleanup();
		}

		if (!m_Trailer || !m_Trailer.GetTractor() || timeSlice <= 0)
		{
			m_iMeasure = 0;
			return;
		}

		vector pos = owner.GetOrigin();
		vector vel = (pos - m_vLastPos) / timeSlice;
		m_vLastPos = pos;

		// first frame: no speed yet, second one: no acceleration yet. A jump (teleport, game
		// master) starts it over.
		if (m_iMeasure < 2 || vel.LengthSq() > TELEPORT_SPEED * TELEPORT_SPEED)
		{
			if (m_iMeasure == 1)
			{
				m_vVel = vel;
				m_vAccel = vector.Zero;
				m_fSettle = SETTLE_TIME;
				m_iMeasure = 2;
			}
			else
			{
				m_iMeasure = 1;
			}

			return;
		}

		vector lastVel = m_vVel;
		m_vVel = m_vVel + (vel - m_vVel) * Math.Min(1, timeSlice / VEL_SMOOTH);
		vector accel = (m_vVel - lastVel) / timeSlice;
		m_vAccel = m_vAccel + (accel - m_vAccel) * Math.Min(1, timeSlice / ACCEL_SMOOTH);

		if (m_fSettle > 0)
		{
			m_fSettle -= timeSlice;
			return;
		}

		// only along and across the deck, bumps go into the suspension
		vector right = owner.GetTransformAxis(0);
		vector fwd = owner.GetTransformAxis(2);
		float lateral = vector.Dot(m_vAccel, right);
		float along = vector.Dot(m_vAccel, fwd);
		float accelH = Math.Sqrt(lateral * lateral + along * along);
		float tilt = Math.Acos(Math.Clamp(owner.GetTransformAxis(1)[1], -1, 1)) * Math.RAD2DEG;

		bool looseHit = accelH > m_fLooseAccel || tilt > m_fLooseTilt;
		if (accelH <= m_fWearAccel && !looseHit)
			return;

		array<IEntity> loads = {};
		m_Trailer.GetStrapLoads(loads);
		if (loads.IsEmpty())
			return;

		// a load is thrown against the acceleration: turning right throws it off the left side
		float side = 1;
		if (lateral > 0.5)
			side = -1;
		else if (lateral > -0.5 && Math.RandomFloat01() < 0.5)
			side = -1;

		array<IEntity> lost = {};
		bool changed = false;
		foreach (IEntity load : loads)
		{
			int i = IndexOf(load);
			if (i < 0)
			{
				if (looseHit)
					lost.Insert(load);

				continue;
			}

			if (accelH <= m_fWearAccel || i >= m_aWear.Count() || GetMode(i) != STRAP_NORMAL)
				continue;

			m_aWear[i] = m_aWear[i] + (accelH - m_fWearAccel) * timeSlice / m_fWearTime;
			if (m_aWear[i] >= 1)
			{
				RemoveAt(i);
				changed = true;
				lost.Insert(load);
				continue;
			}

			int stage = StageOf(m_aWear[i]);
			if (stage != m_aStage[i])
			{
				m_aStage[i] = stage;
				changed = true;
			}
		}

		if (changed)
		{
			Replication.BumpMe();
			OnStrapsChanged();
		}

		if (lost.IsEmpty())
			return;

		int dropped = 0;
		foreach (IEntity gone : lost)
		{
			if (m_Trailer.DropStrapLoad(gone, side, m_vVel))
				dropped++;
		}

		if (dropped == 0)
			return;

		Rpc(RpcDo_LoadLost);
		RpcDo_LoadLost();
	}

	//------------------------------------------------------------------------------------------------
	protected static int StageOf(float wear)
	{
		if (wear >= BREAK_SOON_AT)
			return 2;

		if (wear >= LOOSE_AT)
			return 1;

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: forget loads that are not on the deck any more
	protected void Cleanup()
	{
		if (m_aIds.IsEmpty() || !m_Trailer)
			return;

		array<IEntity> loads = {};
		m_Trailer.GetStrapLoads(loads);

		bool changed = false;
		for (int i = m_aIds.Count() - 1; i >= 0; i--)
		{
			IEntity load = FindEntity(m_aIds[i]);
			if (load && loads.Contains(load))
				continue;

			RemoveAt(i);
			changed = true;
		}

		if (!changed)
			return;

		Replication.BumpMe();
		OnStrapsChanged();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnStrapsChanged()
	{
		if (!m_bDraw)
			return;

		// a different set of loads or a different way of strapping them, not only looser straps
		bool changed = m_aDrawnIds.Count() != m_aIds.Count();
		foreach (int i, RplId id : m_aIds)
		{
			int was = m_aDrawnIds.Find(id);
			if (was < 0 || m_aDrawnModes[was] != GetMode(i))
				changed = true;
		}

		m_aDrawnIds.Copy(m_aIds);
		m_aDrawnModes.Clear();
		foreach (int n, RplId strappedId : m_aIds)
		{
			m_aDrawnModes.Insert(GetMode(n));
		}

		UpdatePieces(GetOwner(), changed);
		ShowStageHints();
	}

	//------------------------------------------------------------------------------------------------
	//! The people in the vehicle at the front of the train get told when the straps work loose
	protected void ShowStageHints()
	{
		int worst = 0;
		array<RplId> ids = {};
		array<int> stages = {};
		foreach (int i, RplId id : m_aIds)
		{
			if (i >= m_aStage.Count())
				break;

			int stage = m_aStage[i];
			ids.Insert(id);
			stages.Insert(stage);

			int before = 0;
			int known = m_aHintIds.Find(id);
			if (known >= 0)
				before = m_aHintStage[known];

			if (stage > before)
				worst = Math.Max(worst, stage);
		}

		m_aHintIds = ids;
		m_aHintStage = stages;

		if (worst == 0 || !IsLocalPlayerAboard())
			return;

		if (worst == 1)
			SCR_HintManagerComponent.ShowCustomHint("The cargo straps are working loose. Stop and tighten them.", "Cargo straps", HINT_TIME);
		else
			SCR_HintManagerComponent.ShowCustomHint("The cargo straps are about to break! Stop and tighten them.", "Cargo straps", HINT_TIME);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_LoadLost()
	{
		if (m_bDraw && IsLocalPlayerAboard())
			SCR_HintManagerComponent.ShowCustomHint("Load lost! Something came off the trailer.", "Cargo straps", HINT_TIME);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsLocalPlayerAboard()
	{
		if (!m_Trailer || !m_Trailer.GetTractor())
			return false;

		IEntity player = SCR_PlayerController.GetLocalControlledEntity();
		if (!player)
			return false;

		IEntity vehicle = CompartmentAccessComponent.GetVehicleIn(player);
		return vehicle && vehicle == SST_SemiTractorComponent.GetChainHead(GetOwner());
	}

	//------------------------------------------------------------------------------------------------
	//! Rebuilds the strap pieces when a strapped load moved on the deck or the straps changed
	//! Building them traces the loads, that is done only when the strapped loads change, never for
	//! straps that work loose (that would be while driving)
	protected void UpdatePieces(IEntity owner, bool force)
	{
		string drawn = string.Empty;
		foreach (int i, RplId id : m_aIds)
		{
			IEntity load = FindEntity(id);
			if (!load || load.GetParent() != owner)
				continue;

			vector local[4];
			load.GetLocalTransform(local);
			drawn += string.Format("%1:%2;", i, local[3].ToString(false));
		}

		if (!force && drawn == m_sDrawn)
			return;

		m_sDrawn = drawn;
		DeletePieces();

		foreach (int n, RplId strapped : m_aIds)
		{
			if (GetMode(n) == STRAP_HIDDEN)
				continue;

			IEntity onDeck = FindEntity(strapped);
			if (onDeck && onDeck.GetParent() == owner)
				BuildStraps(owner, onDeck);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void DeletePieces()
	{
		foreach (IEntity piece : m_aPieces)
		{
			if (piece)
				delete piece;
		}

		m_aPieces.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Straps around the box the load takes on the deck
	protected void BuildStraps(IEntity owner, IEntity load)
	{
		vector mins, maxs;
		m_Trailer.GetStrapBounds(load, mins, maxs);

		// box of the load in trailer space
		vector local[4];
		load.GetLocalTransform(local);
		vector lo = Vector(float.MAX, float.MAX, float.MAX);
		vector hi = Vector(-float.MAX, -float.MAX, -float.MAX);
		for (int c = 0; c < 8; c++)
		{
			float x = mins[0];
			float y = mins[1];
			float z = mins[2];
			if ((c & 1) != 0)
				x = maxs[0];
			if ((c & 2) != 0)
				y = maxs[1];
			if ((c & 4) != 0)
				z = maxs[2];

			vector p = local[3] + local[0] * x + local[1] * y + local[2] * z;
			for (int k = 0; k < 3; k++)
			{
				lo[k] = Math.Min(lo[k], p[k]);
				hi[k] = Math.Max(hi[k], p[k]);
			}
		}

		float sag = 0;

		// on the deck the load is out of the physics world, the traces need it in there
		Physics phys = load.GetPhysics();
		bool wasOff = phys && phys.GetSimulationState() == SimulationState.NONE;
		if (wasOff)
			phys.ChangeSimulationState(SimulationState.COLLISION);

		// vehicles are held by their tyres, helicopters by their rotor blades, everything else gets
		// the straps over the top
		VehicleWheeledSimulation sim = VehicleWheeledSimulation.Cast(load.FindComponent(VehicleWheeledSimulation));
		VehicleHelicopterSimulation heli = VehicleHelicopterSimulation.Cast(load.FindComponent(VehicleHelicopterSimulation));
		if (sim && sim.WheelCount() > 0)
			StrapWheels(owner, load, sim, local, sag);
		else if (!heli || !StrapRotor(owner, load, heli, local, lo, hi, sag))
			StrapAround(owner, load, local, lo, hi, sag);

		if (wasOff)
			phys.ChangeSimulationState(SimulationState.NONE);
	}

	//------------------------------------------------------------------------------------------------
	//! The main rotor is tied down: where the blades are over the deck, straps across the trailer from
	//! one deck edge over the helicopter and the blade to the other, pulled tight over both.
	//! The blades are found with traces around the hub, the rotor can stand at any angle.
	//! False when no blade was found, the helicopter then gets the straps over the top.
	protected bool StrapRotor(IEntity owner, IEntity load, VehicleHelicopterSimulation heli, vector local[4], vector lo, vector hi, float sag)
	{
		vector com;
		Physics phys = load.GetPhysics();
		if (phys)
			com = phys.GetCenterOfMass();

		vector deckMins, deckMaxs;
		owner.GetBounds(deckMins, deckMaxs);

		for (int r = 0, rotors = heli.RotorCount(); r < rotors; r++)
		{
			// the main rotor turns about the vertical, the tail rotor sideways
			vector axis;
			heli.RotorGetAxis(r, axis);
			if (Math.AbsFloat(axis[1]) < 0.7)
				continue;

			if (axis[1] < 0)
				axis = axis * -1;

			axis.Normalize();
			float radius = heli.RotorGetRadius(r);
			vector rotor[4];
			heli.RotorGetTransform(r, rotor);

			// the UH-1H has it in vehicle space (hub 4.49 above the ground), the centre of mass is
			// tried as well in case another helicopter has it like the wheels
			vector hub = rotor[3];
			array<vector> blades = {};
			FindBlades(owner, load, local, hub, axis, radius, blades);
			if (blades.IsEmpty())
			{
				hub = rotor[3] + com;
				FindBlades(owner, load, local, hub, axis, radius, blades);
			}

			if (blades.IsEmpty())
				continue;

			// where along the deck the blades are, sorted front to rear
			array<float> spots = {};
			foreach (vector bladeDir : blades)
			{
				for (int k = 0; k < ROTOR_STRAPS; k++)
				{
					float along = radius * (0.25 + 0.6 * k / (ROTOR_STRAPS - 1));
					vector onBlade;
					if (!TraceDown(owner, load, local, hub + bladeDir * along, axis, onBlade))
						continue;

					vector point = local[3] + local[0] * onBlade[0] + local[1] * onBlade[1] + local[2] * onBlade[2];
					if (point[2] < deckMins[2] + 0.3 || point[2] > deckMaxs[2] - 0.3 || Math.AbsFloat(point[0]) > m_fDeckHalfWidth - 0.1)
						continue;

					int at = 0;
					while (at < spots.Count() && spots[at] > point[2])
					{
						at++;
					}

					spots.InsertAt(point[2], at);
				}
			}

			// one strap per ring pair the blades are over; over the cabin it rests on the roof edge
			vector hubTrailer = local[3] + local[0] * hub[0] + local[1] * hub[1] + local[2] * hub[2];
			int drawn = 0;
			float lastTie = float.MAX;
			foreach (float z : spots)
			{
				float tieZ = TieZ(z);
				if (lastTie - tieZ < MIN_STRAP_GAP)
					continue;

				lastTie = tieZ;
				array<vector> strap = {};
				if (BuildAcross(owner, load, lo, hi, z, tieZ, hubTrailer[1] + 1, strap) == 0)
					continue;

				DrawStrap(owner, strap, sag);
				drawn++;
			}

			if (drawn > 0)
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Directions of the blades from the hub (vehicle space): traces down onto a circle around the hub,
	//! every run of hits is one blade
	protected void FindBlades(IEntity owner, IEntity load, vector local[4], vector hub, vector axis, float radius, notnull array<vector> blades)
	{
		vector e1 = Vector(0, 0, 1) - axis * axis[2];
		e1.Normalize();
		vector e2 = axis * e1;

		array<bool> hit = {};
		for (int i = 0; i < BLADE_SAMPLES; i++)
		{
			float angle = Math.PI2 * i / BLADE_SAMPLES;
			vector dir = e1 * Math.Cos(angle) + e2 * Math.Sin(angle);
			vector unused;
			hit.Insert(TraceDown(owner, load, local, hub + dir * (radius * 0.6), axis, unused));
		}

		// start counting at a sample without a hit, so no blade is cut in two
		int start = hit.Find(false);
		if (start < 0)
			return;

		vector sum = vector.Zero;
		for (int n = 1; n <= BLADE_SAMPLES; n++)
		{
			int idx = (start + n) % BLADE_SAMPLES;
			if (hit[idx])
			{
				float a = Math.PI2 * idx / BLADE_SAMPLES;
				sum = sum + e1 * Math.Cos(a) + e2 * Math.Sin(a);
				continue;
			}

			if (sum.LengthSq() > 0)
			{
				sum.Normalize();
				blades.Insert(sum);
				sum = vector.Zero;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Trace along -axis through point (vehicle space) against the load and its parts (the rotor can
	//! be an entity of its own), hit in vehicle space. Only half a metre either side of the rotor
	//! plane, so the tail boom and the engine cowling below it are not taken for a blade.
	protected bool TraceDown(IEntity owner, IEntity load, vector local[4], vector point, vector axis, out vector hitPos)
	{
		vector start = point + axis * 0.5;
		vector end = point - axis * 0.6;

		TraceParam param = new TraceParam();
		param.Start = owner.CoordToParent(local[3] + local[0] * start[0] + local[1] * start[1] + local[2] * start[2]);
		param.End = owner.CoordToParent(local[3] + local[0] * end[0] + local[1] * end[1] + local[2] * end[2]);
		param.Flags = TraceFlags.ENTS;
		param.TargetLayers = EPhysicsLayerDefs.FireGeometry | EPhysicsLayerDefs.ViewGeometry | EPhysicsLayerDefs.VehicleSimple;

		s_TraceLoad = load;
		float frac = owner.GetWorld().TraceMove(param, IsPartOfLoad);
		if (frac >= 1)
			return false;

		hitPos = start + (end - start) * frac;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsPartOfLoad(notnull IEntity e)
	{
		IEntity entity = e;
		while (entity)
		{
			if (entity == s_TraceLoad)
				return true;

			entity = entity.GetParent();
		}

		return false;
	}


	//------------------------------------------------------------------------------------------------
	//! A wheel strap on every tyre: from the deck in front of it over the tread to the deck behind it.
	//! The simulation has the wheels relative to the centre of mass and not quite in the middle of the
	//! tyre. The model has a bone in the middle of each wheel (v_wheel_L01 on the M998), the wheel
	//! bone closest to the simulated wheel is taken where there is one.
	protected void StrapWheels(IEntity owner, IEntity load, VehicleWheeledSimulation sim, vector local[4], float sag)
	{
		vector com;
		Physics phys = load.GetPhysics();
		if (phys)
			com = phys.GetCenterOfMass();

		array<vector> bones = {};
		Animation anim = load.GetAnimation();
		if (anim)
		{
			array<string> names = {};
			anim.GetBoneNames(names);
			foreach (string boneName : names)
			{
				string lower = boneName;
				lower.ToLower();
				if (!lower.Contains("wheel") || lower.Contains("steering") || lower.Contains("iktarget") || lower.Contains("rotator"))
					continue;

				vector bone[4];
				if (anim.GetBoneMatrix(anim.GetBoneIndex(boneName), bone))
					bones.Insert(bone[3]);
			}
		}

		for (int i = 0, n = sim.WheelCount(); i < n; i++)
		{
			float radius = sim.WheelGetRadius(i);
			if (radius < 0.1)
				continue;

			vector simPos = sim.WheelGetPosition(i) + com;
			vector p = simPos;
			float best = radius * radius;
			foreach (vector bonePos : bones)
			{
				float distSq = vector.DistanceSq(bonePos, simPos);
				if (distSq < best)
				{
					best = distSq;
					p = bonePos;
				}
			}

			// the tyre stands on the deck and can be bigger than the simulated wheel (M151: wheel centre
			// 0.42 above the ground), the strap has to go over the visible one
			radius = Math.Max(radius, p[1]);

			vector centre = local[3] + local[0] * p[0] + local[1] * p[1] + local[2] * p[2];
			StrapWheel(owner, centre, radius, sag);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One tyre (centre in trailer space). The strap runs in the middle of the tread and leaves the
	//! tyre where the line to the deck edge touches it, from there to the lashing ring in front of and
	//! behind the tyre.
	protected void StrapWheel(IEntity owner, vector centre, float radius, float sag)
	{
		float side = 1;
		if (centre[0] < 0)
			side = -1;

		float wrap = radius + STRAP_CLEARANCE + sag * 0.5;

		float frontZ = centre[2] + radius + WHEEL_TIE_AHEAD;
		float rearZ = centre[2] - radius - WHEEL_TIE_AHEAD;
		float ringZ;
		if (m_iRingCount > 0 && RingBeyond(centre[2] + radius * 0.8, 1, ringZ))
			frontZ = ringZ;

		if (m_iRingCount > 0 && RingBeyond(centre[2] - radius * 0.8, -1, ringZ))
			rearZ = ringZ;

		vector frontRing, frontEdge, rearRing, rearEdge;
		float xOut = Math.AbsFloat(centre[0]) + WHEEL_TIE_OUT;
		if (m_iRingCount > 0)
			xOut = 0;

		TiePoints(side, frontZ, xOut, frontRing, frontEdge);
		TiePoints(side, rearZ, xOut, rearRing, rearEdge);

		float angleFront = TangentAngle(frontEdge[2] - centre[2], frontEdge[1] - centre[1], wrap);
		float angleRear = Math.PI - TangentAngle(centre[2] - rearEdge[2], rearEdge[1] - centre[1], wrap);

		vector outward = Vector(side, 0, 0);
		if (vector.DistanceSq(frontRing, frontEdge) > 0.0001)
			Piece(owner, frontRing, frontEdge, outward);

		vector prev = centre + Vector(0, Math.Sin(angleFront), Math.Cos(angleFront)) * wrap;
		Strap(owner, frontEdge, prev, Vector(0, Math.Sin(angleFront), Math.Cos(angleFront)), outward, sag);
		for (int k = 1; k <= WHEEL_ARC_PIECES; k++)
		{
			float angle = angleFront + (angleRear - angleFront) * k / WHEEL_ARC_PIECES;
			vector radial = Vector(0, Math.Sin(angle), Math.Cos(angle));
			vector next = centre + radial * wrap;
			Piece(owner, prev, next, radial);
			prev = next;
		}

		Strap(owner, prev, rearEdge, Vector(0, Math.Sin(angleRear), Math.Cos(angleRear)), outward, sag);
		if (vector.DistanceSq(rearRing, rearEdge) > 0.0001)
			Piece(owner, rearEdge, rearRing, outward);
	}

	//------------------------------------------------------------------------------------------------
	//! Angle (from +z towards +y) where the line from a tie point (dz ahead, dy above the tyre centre)
	//! touches the tyre from above
	protected static float TangentAngle(float dz, float dy, float wrap)
	{
		float d = Math.Sqrt(dz * dz + dy * dy);
		if (d <= wrap)
			return 0.5 * Math.PI;

		return Math.Atan2(dy, dz) + Math.Acos(wrap / d);
	}
	//------------------------------------------------------------------------------------------------
	//! Straps across the load from ring to ring, pulled tight over it. A strap goes where a ring pair is
	//! close enough, otherwise it runs a bit slanted to the nearest rings.
	protected void StrapAround(IEntity owner, IEntity load, vector local[4], vector lo, vector hi, float sag)
	{
		float length = hi[2] - lo[2];
		float inset = Math.Min(0.4, length * 0.15);
		float usable = length - inset * 2;
		int count = 1;
		if (length >= 1)
			count = (int)Math.Ceil(usable / m_fStrapSpacing) + 1;

		// the wheels have no collision the traces could find, the straps go between them instead
		array<float> spanLo = {};
		array<float> spanHi = {};
		GetWheelSpans(load, local, spanLo, spanHi);

		float lastTie = float.MAX;
		for (int k = 0; k < count; k++)
		{
			float z = (lo[2] + hi[2]) * 0.5;
			if (count > 1)
				z = hi[2] - inset - usable * k / (count - 1);

			float tieZ = TieZ(z);
			if (tieZ > lo[2] + 0.1 && tieZ < hi[2] - 0.1 && Math.AbsFloat(tieZ - z) < m_fStrapSpacing * 0.5)
				z = tieZ;

			z = AvoidWheels(z, lo[2] + 0.1, hi[2] - 0.1, spanLo, spanHi);
			if (lastTie - tieZ < MIN_STRAP_GAP)
				continue;

			lastTie = tieZ;
			array<vector> strap = {};
			BuildAcross(owner, load, lo, hi, z, tieZ, hi[1] + 0.3, strap);
			DrawStrap(owner, strap, sag);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! z of the ring pair closest to z, z itself without rings
	protected float TieZ(float z)
	{
		if (m_iRingCount <= 0 || m_fRingSpacing <= 0)
			return z;

		int ring = Math.ClampInt((int)Math.Round((m_fRingFront - z) / m_fRingSpacing), 0, m_iRingCount - 1);
		return m_fRingFront - m_fRingSpacing * ring;
	}

	//------------------------------------------------------------------------------------------------
	//! Ring pair closest to z that is at least that far ahead (dir 1) or behind (dir -1)
	protected bool RingBeyond(float z, float dir, out float ringZ)
	{
		bool found = false;
		for (int i = 0; i < m_iRingCount; i++)
		{
			float rz = m_fRingFront - m_fRingSpacing * i;
			if ((rz - z) * dir < 0)
				continue;

			if (!found || Math.AbsFloat(rz - z) < Math.AbsFloat(ringZ - z))
				ringZ = rz;

			found = true;
		}

		return found;
	}

	//------------------------------------------------------------------------------------------------
	//! Where a strap on side (-1 left, 1 right) is fastened at tieZ: the ring, and the deck edge it
	//! comes up over when the ring hangs below it. Ring and edge are the same point without rings or
	//! when the ring is not lower than the deck. x is moved out to xOut when the load is wider.
	protected void TiePoints(float side, float tieZ, float xOut, out vector ring, out vector edge)
	{
		float edgeX = side * Math.Max(m_fDeckHalfWidth, xOut);
		edge = Vector(edgeX, m_fDeckY, tieZ);
		ring = edge;
		if (m_iRingCount <= 0)
			return;

		ring = Vector(side * m_fRingX, m_fRingY, tieZ);
		if (m_fRingY >= m_fDeckY - 0.01)
		{
			ring[0] = side * Math.Max(m_fRingX, xOut);
			edge = ring;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One strap across the trailer (trailer space): ring, deck edge, pulled tight over the load at
	//! zPlane, deck edge, ring. Returns how many points of the load it was measured on.
	protected int BuildAcross(IEntity owner, IEntity load, vector lo, vector hi, float zPlane, float tieZ, float yTop, notnull array<vector> strap)
	{
		float xOut = Math.Max(-lo[0], hi[0]) + 0.03;
		vector ringL, edgeL, ringR, edgeR;
		TiePoints(-1, tieZ, xOut, ringL, edgeL);
		TiePoints(1, tieZ, xOut, ringR, edgeR);

		float xFrom = edgeL[0] + 0.02;
		float xTo = edgeR[0] - 0.02;
		int samples = (int)Math.Ceil((xTo - xFrom) / PROFILE_STEP) + 1;

		array<vector> points = {};
		points.Insert(edgeL);
		int hits = TraceProfile(owner, load, xFrom, xTo, yTop, m_fDeckY - 0.05, zPlane, samples, points);
		if (hits == 0)
		{
			points.Insert(Vector(lo[0], hi[1] + STRAP_CLEARANCE, zPlane));
			points.Insert(Vector(hi[0], hi[1] + STRAP_CLEARANCE, zPlane));
		}

		points.Insert(edgeR);

		array<vector> hull = {};
		UpperHull(points, hull);

		if (vector.DistanceSq(ringL, edgeL) > 0.0001)
			strap.Insert(ringL);

		foreach (vector p : hull)
		{
			strap.Insert(p);
		}

		if (vector.DistanceSq(ringR, edgeR) > 0.0001)
			strap.Insert(ringR);

		return hits;
	}

	//------------------------------------------------------------------------------------------------
	//! Draws a strap along the points. Slack bulges out on the two pieces that come up from the deck.
	protected void DrawStrap(IEntity owner, notnull array<vector> strap, float sag)
	{
		int last = strap.Count() - 2;
		int legL = 0;
		int legR = last;
		if (last >= 2 && strap[0][1] < m_fDeckY - 0.01)
		{
			legL = 1;
			legR = last - 1;
		}

		for (int e = 0; e <= last; e++)
		{
			vector a = strap[e];
			vector b = strap[e + 1];
			vector normal = Vector(a[1] - b[1], b[0] - a[0], 0);
			if (normal.LengthSq() < 0.0001)
				normal = Vector(0, 1, 0);

			normal.Normalize();

			float loose = 0;
			if (e == legL || e == legR)
				loose = sag;

			Strap(owner, a, b, normal, normal, loose);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Where along the deck (trailer z) the tyres of the load are, overlapping ones merged, front to rear
	protected void GetWheelSpans(IEntity load, vector local[4], notnull array<float> spanLo, notnull array<float> spanHi)
	{
		VehicleWheeledSimulation sim = VehicleWheeledSimulation.Cast(load.FindComponent(VehicleWheeledSimulation));
		if (!sim)
			return;

		array<float> los = {};
		array<float> his = {};
		for (int i = 0, n = sim.WheelCount(); i < n; i++)
		{
			vector p = sim.WheelGetPosition(i);
			float r = sim.WheelGetRadius(i) + WHEEL_MARGIN;
			vector centre = local[3] + local[0] * p[0] + local[1] * p[1] + local[2] * p[2];
			float z = centre[2];

			// sorted by front edge
			int at = 0;
			while (at < los.Count() && los[at] < z - r)
			{
				at++;
			}

			los.InsertAt(z - r, at);
			his.InsertAt(z + r, at);
		}

		foreach (int j, float front : los)
		{
			int last = spanHi.Count() - 1;
			if (last >= 0 && front <= spanHi[last])
			{
				spanHi[last] = Math.Max(spanHi[last], his[j]);
				continue;
			}

			spanLo.Insert(front);
			spanHi.Insert(his[j]);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! z moved out of a tyre to the nearer free side, as long as that stays on the load
	protected static float AvoidWheels(float z, float minZ, float maxZ, array<float> spanLo, array<float> spanHi)
	{
		foreach (int i, float spanStart : spanLo)
		{
			float spanEnd = spanHi[i];
			if (z <= spanStart || z >= spanEnd)
				continue;

			bool startOk = spanStart >= minZ;
			bool endOk = spanEnd <= maxZ;
			if (startOk && (!endOk || z - spanStart < spanEnd - z))
				return spanStart;

			if (endOk)
				return spanEnd;

			return z;
		}

		return z;
	}

	//------------------------------------------------------------------------------------------------
	//! Top of the load across the deck at z (trailer space), left to right, a bit above the surface.
	//! The fire geometry follows the model, the vehicle colliders are boxes around it (a box around
	//! each rotor blade on the UH-1H): those only where a load has no fire geometry at all.
	protected int TraceProfile(IEntity owner, IEntity load, float xFrom, float xTo, float yTop, float yBottom, float z, int samples, notnull array<vector> points)
	{
		int hits = TraceProfileLayers(owner, load, xFrom, xTo, yTop, yBottom, z, samples, EPhysicsLayerDefs.FireGeometry, points);
		if (hits == 0)
			hits = TraceProfileLayers(owner, load, xFrom, xTo, yTop, yBottom, z, samples, EPhysicsLayerDefs.FireGeometry | EPhysicsLayerDefs.ViewGeometry | EPhysicsLayerDefs.VehicleSimple, points);

		return hits;
	}

	//------------------------------------------------------------------------------------------------
	protected int TraceProfileLayers(IEntity owner, IEntity load, float xFrom, float xTo, float yTop, float yBottom, float z, int samples, int layers, notnull array<vector> points)
	{
		BaseWorld world = owner.GetWorld();
		s_TraceLoad = load;
		int hits = 0;
		for (int i = 0; i < samples; i++)
		{
			float x = xFrom;
			if (samples > 1)
				x = xFrom + (xTo - xFrom) * i / (samples - 1);

			TraceParam param = new TraceParam();
			param.Start = owner.CoordToParent(Vector(x, yTop, z));
			param.End = owner.CoordToParent(Vector(x, yBottom, z));
			param.Flags = TraceFlags.ENTS;
			param.TargetLayers = layers;

			float frac = world.TraceMove(param, IsPartOfLoad);
			if (frac >= 1)
				continue;

			points.Insert(Vector(x, yTop + (yBottom - yTop) * frac + STRAP_CLEARANCE, z));
			hits++;
		}

		return hits;
	}
	//------------------------------------------------------------------------------------------------
	//! Upper hull of points sorted left to right: a strap pulled tight over them
	protected static void UpperHull(notnull array<vector> points, notnull array<vector> hull)
	{
		foreach (vector p : points)
		{
			while (hull.Count() >= 2)
			{
				vector a = hull[hull.Count() - 2];
				vector b = hull[hull.Count() - 1];
				float cross = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
				if (cross < 0)
					break;

				hull.Remove(hull.Count() - 1);
			}

			hull.Insert(p);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! One strap from a to b (trailer space). A loose strap gets a kink in the middle towards sagDir.
	protected void Strap(IEntity owner, vector a, vector b, vector normal, vector sagDir, float sag)
	{
		if (sag <= 0)
		{
			Piece(owner, a, b, normal);
			return;
		}

		vector mid = (a + b) * 0.5 + sagDir * (sag * vector.Distance(a, b));
		Piece(owner, a, mid, normal);
		Piece(owner, mid, b, normal);
	}

	//------------------------------------------------------------------------------------------------
	//! The strap piece stretched from a to b, flat side towards normal
	protected void Piece(IEntity owner, vector a, vector b, vector normal)
	{
		vector dir = b - a;
		float length = dir.Length();
		if (length < 0.01)
			return;

		dir = dir / length;
		vector up = normal - dir * vector.Dot(normal, dir);
		if (up.LengthSq() < 0.0001)
			up = Vector(0, 1, 0) - dir * dir[1];

		if (up.LengthSq() < 0.0001)
			return;

		up.Normalize();
		vector side = up * dir;

		if (!m_StrapRes)
			m_StrapRes = Resource.Load(m_sStrapPrefab);

		if (!m_StrapRes || !m_StrapRes.IsValid())
			return;

		IEntity piece = GetGame().SpawnEntityPrefabLocal(m_StrapRes, owner.GetWorld(), null);
		if (!piece)
			return;

		// with a hierarchy component it moves with the trailer in the same frame, without it lags behind
		owner.AddChild(piece, -1, EAddChildFlags.AUTO_TRANSFORM);
		vector mat[4];
		mat[0] = side;
		mat[1] = up;
		mat[2] = dir * length;
		mat[3] = a;
		piece.SetLocalTransform(mat);
		piece.Update();
		m_aPieces.Insert(piece);
	}
}
