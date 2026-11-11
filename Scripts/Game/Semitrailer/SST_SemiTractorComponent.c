// Tractor side of the semitrailer. Spawns (optionally) a trailer, couples and uncouples it and moves it.
//
// There is no physics joint: a joint between a wheeled vehicle and a heavy trailer stretches by
// metres while driving. The kingpin is kept on the fifth wheel instead and the trailer follows the
// tractor like a real semitrailer would (the bogie trails behind the kingpin). Up/down motion of the
// bogie, roll and ground contact are integrated here.
//
// Whoever owns the tractor (usually the driver) runs this and sends the trailer pose relative to
// the tractor; all other machines just place their copy behind their own tractor.
// The trailer body is kinematic everywhere, it only collides.
//
// Coupling works like on a real fifth wheel: back the tractor under a parked trailer and the jaws
// lock once the kingpin is on the plate. Uncoupling is done at the trailer (user action), the trailer
// then stays standing on its landing legs. Coupling/uncoupling is decided by the server.
//
// Road trains: a trailer can have this component too. With m_bTowHitch it is a tow hitch at the rear
// that takes a drawbar trailer (dolly), the dolly has it as its fifth wheel. A towed trailer does not
// move its own trailer in its frame event, the tractor in front does that right after it moved it,
// so the whole train is posed in order every frame.

[ComponentEditorProps(category: "SST/Semitrailer", description: "Tractor, spawns and tows a semitrailer")]
class SST_SemiTractorComponentClass : ScriptGameComponentClass
{
}

class SST_SemiTractorComponent : ScriptGameComponent
{
	[Attribute("", UIWidgets.ResourceNamePicker, "Trailer spawned together with the tractor", params: "et")]
	protected ResourceName m_sTrailerPrefab;

	[Attribute("0 1.37 -2.3", UIWidgets.Coords, "Kingpin position on the fifth wheel, tractor space")]
	protected vector m_vFifthWheel;

	[Attribute("70", UIWidgets.EditBox, "Max articulation angle [deg]", category: "Coupling")]
	protected float m_fMaxArticulation;

	[Attribute("15", UIWidgets.EditBox, "Max pitch against the tractor [deg]", category: "Coupling")]
	protected float m_fMaxPitch;

	[Attribute("6", UIWidgets.EditBox, "Max roll against the tractor [deg]", category: "Coupling")]
	protected float m_fMaxRoll;

	[Attribute("0.45", UIWidgets.EditBox, "Kingpin has to be this close to the fifth wheel to lock [m]", category: "Coupling")]
	protected float m_fCoupleRadius;

	[Attribute("1.5", UIWidgets.EditBox, "Max tractor speed for coupling and uncoupling [m/s]", category: "Coupling")]
	protected float m_fCoupleSpeed;

	[Attribute("0", UIWidgets.CheckBox, "Tow hitch instead of a fifth wheel: only drawbar trailers (dolly) couple here", category: "Coupling")]
	protected bool m_bTowHitch;

	[Attribute("0", UIWidgets.EditBox, "Coupling by hand (user action) works up to this distance, 0 = what the action allows [m]", category: "Coupling")]
	protected float m_fHandCoupleRadius;

	[RplProp(onRplName: "OnTrailerChanged")]
	protected RplId m_TrailerId;

	[RplProp()]
	protected vector m_vRelPos;

	[RplProp()]
	protected vector m_vRelAng;

	protected IEntity m_Trailer;
	protected RplComponent m_Rpl;
	protected SST_SemiTrailerComponent m_TrailerComp;
	protected BaseLightManagerComponent m_Lights;
	protected IEntity m_LightsOwner;
	protected float m_fLightTimer;
	protected float m_fSendTimer;

	// proxy side, smoothed pose
	protected vector m_vRelPosCur;
	protected vector m_vRelAngCur;
	protected bool m_bRelInit;

	// owner side
	protected bool m_bHasState;
	protected vector m_vBogie;			// only x/z are used
	protected vector m_vHeading;
	protected float m_fAxleY;
	protected float m_fAxleVel;
	protected float m_fRoll;
	protected float m_fRollVel;
	protected float m_fPitch;
	protected vector m_vLastKingpin;
	protected vector m_vKingOffset;		// eases the kingpin onto the plate after coupling
	protected float m_fScanTimer;
	protected float m_fTippedTime;
	protected ref array<float> m_aComp = {};
	protected ref array<float> m_aWheelGround = {};
	protected float m_fTeleportDist = TELEPORT_DIST;
	protected ref array<float> m_aPointGround = {};
	protected ref array<float> m_aPointPen = {};

	protected static IEntity s_TraceTractor;
	protected static IEntity s_TraceTrailer;
	protected static IEntity s_TraceTowed;
	protected static ref array<SST_SemiTractorComponent> s_aTractors = {};

	static const int MAX_CHAIN = 8;
	static const float LIGHT_INTERVAL = 0.1;
	static const float SEND_INTERVAL = 0.05;
	static const float MAX_STEP = 0.004;
	static const float GRAVITY = 9.81;
	static const float TELEPORT_DIST = 6;
	static const float NO_GROUND = -1000000;
	static const float TIPPED_UP = 0.34;		// up axis this low = more than ~70 deg over
	static const float TIPPED_TIME = 0.5;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		m_Rpl = RplComponent.Cast(owner.FindComponent(RplComponent));
		m_vRelPos = m_vFifthWheel;
		SetEventMask(owner, EntityEvent.INIT | EntityEvent.POSTFRAME);

		if (GetGame() && GetGame().InPlayMode())
			s_aTractors.Insert(this);
	}

	//------------------------------------------------------------------------------------------------
	override event bool OnTicksOnRemoteProxy()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		if (GetGame().InPlayMode() && Replication.IsServer() && !m_sTrailerPrefab.IsEmpty())
			GetGame().GetCallqueue().CallLater(SpawnTrailer, 100, false);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		s_aTractors.RemoveItem(this);

		// the trailer stays where it is
		IEntity trailer = GetTrailer();
		if (trailer && !trailer.IsDeleted() && Replication.IsServer() && GetGame() && GetGame().InPlayMode())
			Uncouple();

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void SpawnTrailer()
	{
		IEntity owner = GetOwner();
		if (!owner || m_Trailer || m_sTrailerPrefab.IsEmpty())
			return;

		Resource res = Resource.Load(m_sTrailerPrefab);
		if (!res || !res.IsValid())
			return;

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		owner.GetWorldTransform(params.Transform);
		params.Transform[3] = owner.CoordToParent(m_vFifthWheel);

		IEntity trailer = GetGame().SpawnEntityPrefab(res, owner.GetWorld(), params);
		if (!trailer)
			return;

		// the trailer has to exist wherever the tractor exists
		RplComponent rpl = RplComponent.Cast(trailer.FindComponent(RplComponent));
		if (rpl)
		{
			rpl.EnableSpatialRelevancy(false);
			rpl.EnableStreaming(false);
		}

		Couple(trailer);
	}

	//------------------------------------------------------------------------------------------------
	//! Server only
	bool Couple(IEntity trailer)
	{
		if (!Replication.IsServer() || !trailer || GetTrailer())
			return false;

		SST_SemiTrailerComponent comp = SST_SemiTrailerComponent.Cast(trailer.FindComponent(SST_SemiTrailerComponent));
		RplComponent rpl = RplComponent.Cast(trailer.FindComponent(RplComponent));
		if (!comp || !rpl || comp.GetTractor() || comp.IsWrecked() || !CanTow(trailer))
			return false;

		IEntity owner = GetOwner();
		m_Trailer = trailer;
		m_TrailerComp = comp;
		m_TrailerId = rpl.Id();
		m_bHasState = false;
		m_bRelInit = false;

		// proxies start from where the trailer stands now
		vector relPos, relAng;
		GetRelativePose(owner, trailer, relPos, relAng);
		m_vRelPos = relPos;
		m_vRelAng = relAng;

		comp.OnCoupled(owner);
		Replication.BumpMe();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Server only
	void Uncouple()
	{
		if (!Replication.IsServer() || !GetTrailer())
			return;

		if (!m_TrailerComp)
			m_TrailerComp = SST_SemiTrailerComponent.Cast(m_Trailer.FindComponent(SST_SemiTrailerComponent));

		if (m_TrailerComp)
			m_TrailerComp.OnUncoupled(GetOwner());

		m_TrailerId = RplId.Invalid();
		ClearTrailer();
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	protected void OnTrailerChanged()
	{
		if (DIAG)
			m_iDiagCallbacks++;

		// may come along with the other properties of this component, only another trailer resets us
		if (m_Trailer && m_TrailerId.IsValid())
		{
			RplComponent rpl = RplComponent.Cast(m_Trailer.FindComponent(RplComponent));
			if (rpl && rpl.Id() == m_TrailerId)
				return;
		}

		ClearTrailer();
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearTrailer()
	{
		if (m_TrailerComp && m_TrailerComp.GetTractor() == GetOwner())
		{
			m_TrailerComp.SetTractor(null);
			m_TrailerComp.SwitchLightsOff();
		}

		m_Trailer = null;
		m_TrailerComp = null;
		m_bHasState = false;
		m_bRelInit = false;
	}

	//------------------------------------------------------------------------------------------------
	//! The vehicle at the front of the train this entity is in (the entity itself when nothing tows it)
	static IEntity GetChainHead(IEntity entity)
	{
		for (int i = 0; i < MAX_CHAIN && entity; i++)
		{
			SST_SemiTrailerComponent trailer = SST_SemiTrailerComponent.Cast(entity.FindComponent(SST_SemiTrailerComponent));
			if (!trailer || !trailer.GetTractor())
				return entity;

			entity = trailer.GetTractor();
		}

		return entity;
	}

	//------------------------------------------------------------------------------------------------
	//! Is the owner a trailer that something tows right now
	protected bool IsTowed(IEntity owner)
	{
		SST_SemiTrailerComponent trailer = SST_SemiTrailerComponent.Cast(owner.FindComponent(SST_SemiTrailerComponent));
		return trailer && trailer.GetTractor() != null;
	}

	//------------------------------------------------------------------------------------------------
	//! Fifth wheel takes semitrailers, a tow hitch takes drawbar trailers. Never ourselves or a
	//! trailer that is part of the train in front of us.
	bool CanTow(IEntity trailer)
	{
		IEntity owner = GetOwner();
		SST_SemiTrailerComponent comp = SST_SemiTrailerComponent.Cast(trailer.FindComponent(SST_SemiTrailerComponent));
		if (!comp || trailer == owner || comp.HasDrawbar() != m_bTowHitch)
			return false;

		IEntity entity = owner;
		for (int i = 0; i < MAX_CHAIN && entity; i++)
		{
			SST_SemiTrailerComponent towed = SST_SemiTrailerComponent.Cast(entity.FindComponent(SST_SemiTrailerComponent));
			if (!towed)
				break;

			entity = towed.GetTractor();
			if (entity == trailer)
				return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! A trailer has no moving physics of its own, the head of the train has the speed
	float GetSpeed()
	{
		Physics phys = GetChainHead(GetOwner()).GetPhysics();
		if (!phys)
			return 0;

		return phys.GetVelocity().Length();
	}

	//------------------------------------------------------------------------------------------------
	float GetHandCoupleRadius()
	{
		return m_fHandCoupleRadius;
	}

	//------------------------------------------------------------------------------------------------
	bool CanCoupleNow()
	{
		return GetSpeed() <= m_fCoupleSpeed;
	}

	//------------------------------------------------------------------------------------------------
	//! Is the kingpin of this trailer on (or close to) our fifth wheel
	bool IsInCouplingRange(IEntity trailer, float radius)
	{
		if (!CanTow(trailer))
			return false;

		IEntity owner = GetOwner();
		vector plate = owner.CoordToParent(m_vFifthWheel);
		vector kingpin = trailer.GetOrigin();
		if (vector.DistanceXZ(plate, kingpin) > radius)
			return false;

		// a parked trailer sits a bit lower, the plate lifts it
		float dy = kingpin[1] - plate[1];
		if (dy < -0.6 || dy > 0.3)
			return false;

		vector fwd = owner.GetTransformAxis(2);
		vector trailerFwd = trailer.GetTransformAxis(2);
		fwd[1] = 0;
		trailerFwd[1] = 0;
		fwd.Normalize();
		trailerFwd.Normalize();
		return vector.Dot(fwd, trailerFwd) > 0.7;
	}

	//------------------------------------------------------------------------------------------------
	//! Free tractor with its fifth wheel under the kingpin of the given trailer (by hand)
	static SST_SemiTractorComponent FindTractorFor(IEntity trailer, float radius)
	{
		foreach (SST_SemiTractorComponent tractor : s_aTractors)
		{
			if (tractor && !tractor.GetTrailer() && tractor.IsInCouplingRange(trailer, Math.Max(radius, tractor.GetHandCoupleRadius())))
				return tractor;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: lock the jaws when we back under a parked trailer
	protected void ScanForTrailer(IEntity owner, float timeSlice)
	{
		m_fScanTimer += timeSlice;
		if (m_fScanTimer < 0.25)
			return;

		m_fScanTimer = 0;
		if (!CanCoupleNow())
			return;

		vector plate = owner.CoordToParent(m_vFifthWheel);
		array<SST_SemiTrailerComponent> trailers = SST_SemiTrailerComponent.GetAll();
		foreach (SST_SemiTrailerComponent comp : trailers)
		{
			if (!comp || comp.GetTractor() || comp.IsWrecked())
				continue;

			IEntity trailer = comp.GetOwner();
			float dist = vector.DistanceXZ(plate, trailer.GetOrigin());
			if (dist > 20)
				continue;

			// just uncoupled from us, wait until we drove away
			if (comp.IsBlockedFor(owner))
			{
				if (dist > 2.5)
					comp.ClearBlock();

				continue;
			}

			if (IsInCouplingRange(trailer, m_fCoupleRadius) && Couple(trailer))
				return;
		}
	}

	//------------------------------------------------------------------------------------------------
	IEntity GetTrailer()
	{
		if (!m_Trailer && m_TrailerId.IsValid())
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(m_TrailerId));
			if (rpl)
				m_Trailer = rpl.GetEntity();
		}

		return m_Trailer;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsOwner()
	{
		return !m_Rpl || m_Rpl.IsOwner();
	}

	//------------------------------------------------------------------------------------------------
	//! After the physics update: in EOnFrame the tractor has not moved yet this frame, the trailer
	//! would hang one frame behind it. That is speed * frame time, half a metre at 100 km/h and
	//! changing with every frame, so the trailer jerks against the truck at speed.
	override void EOnPostFrame(IEntity owner, float timeSlice)
	{
		// moved by the tractor in front, which updates us right after that
		if (IsTowed(owner))
			return;

		if (DIAG)
			DiagFrame(owner, timeSlice);

		UpdateTrain(owner, timeSlice, 0);
	}

	//------------------------------------------------------------------------------------------------
	// TEMP lag diagnostics, remove with DIAG
	static const bool DIAG = true;
	protected vector m_vDiagLastPos;
	protected bool m_bDiagInit;
	protected int m_iDiagCallbacks;
	protected int m_iDiagInits;

	protected void DiagFrame(IEntity owner, float timeSlice)
	{
		vector pos = owner.GetOrigin();
		if (!m_bDiagInit)
		{
			m_vDiagLastPos = pos;
			m_bDiagInit = true;
			return;
		}

		Physics phys = owner.GetPhysics();
		vector vel;
		if (phys)
			vel = phys.GetVelocity();

		float speed = vel.Length();
		vector moved = pos - m_vDiagLastPos;
		m_vDiagLastPos = pos;
		if (speed < 25 || timeSlice <= 0)
			return;

		// how far the truck went this frame compared to its velocity, along and across
		vector dir = vel.Normalized();
		float along = vector.Dot(moved, dir);
		float across = (moved - dir * along).Length();

		bool interp;
		NwkMovementComponent nwk = NwkMovementComponent.Cast(owner.FindComponent(NwkMovementComponent));
		if (nwk)
			interp = nwk.IsInterpolating();

		PrintFormat("SSTDIAG srv=%1 own=%2 dt=%3 kmh=%4 jump=%5 across=%6 interp=%7 cb=%8 init=%9", Replication.IsServer(), IsOwner(), timeSlice, speed * 3.6, along - speed * timeSlice, across, interp, m_iDiagCallbacks, m_iDiagInits);
		PrintFormat("SSTDIAG2 koff=%1", m_vKingOffset.Length());
		m_iDiagCallbacks = 0;
		m_iDiagInits = 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Move our trailer, then let it move its own one
	protected void UpdateTrain(IEntity owner, float timeSlice, int depth)
	{
		Tow(owner, timeSlice);

		IEntity trailer = GetTrailer();
		if (!trailer || depth >= MAX_CHAIN)
			return;

		SST_SemiTractorComponent next = SST_SemiTractorComponent.Cast(trailer.FindComponent(SST_SemiTractorComponent));
		if (next)
			next.UpdateTrain(trailer, timeSlice, depth + 1);
	}

	//------------------------------------------------------------------------------------------------
	protected void Tow(IEntity owner, float timeSlice)
	{
		IEntity trailer = GetTrailer();
		if (!trailer)
		{
			if (Replication.IsServer() && GetGame().InPlayMode())
				ScanForTrailer(owner, timeSlice);

			return;
		}

		if (!m_TrailerComp)
		{
			m_TrailerComp = SST_SemiTrailerComponent.Cast(trailer.FindComponent(SST_SemiTrailerComponent));
			if (!m_TrailerComp)
				return;
		}

		// a truck on its side or back drops the trailer, the trailer can't roll over with it
		if (Replication.IsServer() && IsTipped(owner, timeSlice))
		{
			Uncouple();
			return;
		}

		m_TrailerComp.SetTractor(owner);

		// in a road train every trailer takes the lights of the vehicle at the front
		m_fLightTimer += timeSlice;
		if (m_fLightTimer >= LIGHT_INTERVAL)
		{
			m_fLightTimer = 0;
			IEntity head = GetChainHead(owner);
			if (head != m_LightsOwner)
			{
				m_LightsOwner = head;
				m_Lights = BaseLightManagerComponent.Cast(head.FindComponent(BaseLightManagerComponent));
			}

			m_TrailerComp.SyncLights(head, m_Lights);
		}

		// A trailer that tows (road train) runs the simulation of its own trailer on every machine. Taking
		// the pose relative to it from the network would carry every pitch of the trailer in front rigidly
		// to the rear: a bump at the front lifts the end of a long train by metres.
		bool isOwner = IsOwner();
		bool simulate = isOwner || owner.FindComponent(SST_SemiTrailerComponent) != null;
		m_TrailerComp.SetSimulated(simulate);
		if (!simulate)
		{
			m_bHasState = false;
			FollowRelativePose(owner, trailer, timeSlice);
			return;
		}

		m_bRelInit = false;

		// a fast truck covers several metres in a long frame, that is no teleport
		m_fTeleportDist = TELEPORT_DIST + GetSpeed() * timeSlice * 1.5;

		if (timeSlice > 0)
		{
			int steps = Math.Ceil(timeSlice / MAX_STEP);
			if (steps < 1)
				steps = 1;
			else if (steps > 40)
				steps = 40;

			float dt = timeSlice / steps;
			for (int i = 0; i < steps; i++)
			{
				Step(owner, trailer, dt, i == 0);
			}
		}

		ApplyPose(owner, trailer);
		if (!isOwner)
			return;

		m_fSendTimer += timeSlice;
		if (m_fSendTimer < SEND_INTERVAL)
			return;

		m_fSendTimer = 0;
		vector relPos, relAng;
		GetRelativePose(owner, trailer, relPos, relAng);
		if (vector.DistanceSq(relPos, m_vRelPos) < 0.0001 && vector.DistanceSq(relAng, m_vRelAng) < 0.01)
			return;

		m_vRelPos = relPos;
		m_vRelAng = relAng;
		if (Replication.IsServer())
			Replication.BumpMe();
		else
			Rpc(RpcAsk_SetRelative, relPos, relAng);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsTipped(IEntity owner, float timeSlice)
	{
		vector up = owner.GetTransformAxis(1);
		if (up[1] >= TIPPED_UP)
		{
			m_fTippedTime = 0;
			return false;
		}

		m_fTippedTime += timeSlice;
		return m_fTippedTime >= TIPPED_TIME;
	}

	//------------------------------------------------------------------------------------------------
	protected void Step(IEntity tractor, IEntity trailer, float dt, bool firstStep)
	{
		int wheelCount = m_TrailerComp.GetWheelCount();
		if (wheelCount == 0)
			return;

		float length = m_TrailerComp.GetKingpinToBogie();
		float axleOffset = m_TrailerComp.GetAxleY();
		float radius = m_TrailerComp.GetWheelRadius();
		float rest = m_TrailerComp.GetRestCompression();
		float travel = m_TrailerComp.GetTravel();
		float mass = m_TrailerComp.GetMass();

		vector kingpin = tractor.CoordToParent(m_vFifthWheel);
		vector tractorFwd = tractor.GetTransformAxis(2);
		tractorFwd[1] = 0;
		if (tractorFwd.LengthSq() < 0.0001)
			return;

		tractorFwd.Normalize();

		if (!m_bHasState)
			InitState(tractor, trailer, kingpin, tractorFwd, length, radius, wheelCount);
		else if (vector.DistanceXZ(kingpin, m_vLastKingpin) > m_fTeleportDist)
			ResetState(tractor, kingpin, tractorFwd, length, radius, wheelCount);

		m_vLastKingpin = kingpin;

		m_vKingOffset = m_vKingOffset * (1 - Math.Clamp(dt * 4, 0, 1));
		kingpin = kingpin + m_vKingOffset;

		// bogie trails the kingpin
		vector heading = kingpin - m_vBogie;
		heading[1] = 0;
		if (heading.LengthSq() < 0.0001)
			heading = tractorFwd;

		heading.Normalize();

		float maxArt = m_fMaxArticulation * Math.DEG2RAD;
		float art = SignedAngle(tractorFwd, heading);
		if (art > maxArt || art < -maxArt)
			heading = RotateY(tractorFwd, Math.Clamp(art, -maxArt, maxArt));

		m_vHeading = heading;
		m_vBogie = kingpin - heading * length;
		vector right = Vector(heading[2], 0, -heading[0]);

		float restAngle = Math.Atan2(-axleOffset, length);
		m_fPitch = Math.Atan2(kingpin[1] - m_fAxleY, length) - restAngle;

		vector tractorAxisX = tractor.GetTransformAxis(0);
		vector tractorAxisZ = tractor.GetTransformAxis(2);
		float tractorPitch = Math.Asin(Math.Clamp(tractorAxisZ[1], -1, 1));
		float tractorRoll = Math.Asin(Math.Clamp(tractorAxisX[1], -1, 1));

		// wheels
		float bogieMass = mass * m_TrailerComp.GetAxleShare();
		float spring = bogieMass * GRAVITY / (wheelCount * Math.Max(rest, 0.01));
		float damper = 0.8 * Math.Sqrt(spring * bogieMass / wheelCount);
		float sinPitch = Math.Sin(m_fPitch);
		float sinRoll = Math.Sin(m_fRoll);

		// the wheels stand on the same spot in every sub step, so the ground is traced once per frame
		bool traceWheels = firstStep || m_aWheelGround.Count() != wheelCount;
		if (m_aWheelGround.Count() != wheelCount)
		{
			m_aWheelGround.Clear();
			for (int i = 0; i < wheelCount; i++)
			{
				m_aWheelGround.Insert(NO_GROUND);
			}
		}

		float force = 0;
		float torque = 0;
		float halfTrack = 0;
		float maxComp = -1;
		for (int i = 0; i < wheelCount; i++)
		{
			vector local = m_TrailerComp.GetWheelLocal(i);
			float along = local[2] + length;
			float side = local[0];
			halfTrack = Math.Max(halfTrack, Math.AbsFloat(side));

			vector pos = m_vBogie + heading * along + right * side;
			float axleY = m_fAxleY + along * sinPitch + side * sinRoll;
			if (traceWheels)
				m_aWheelGround[i] = GroundY(tractor, trailer, pos, axleY);

			float ground = m_aWheelGround[i];

			float comp = -1;
			if (ground > NO_GROUND)
				comp = ground + m_TrailerComp.GetWheelRadiusAt(i) - (axleY - rest);

			float wheelForce = 0;
			if (comp > 0)
			{
				float clamped = Math.Min(comp, travel);

				// no damping on the first contact, a stone would launch the trailer otherwise
				float rate = 0;
				if (m_aComp[i] > 0)
					rate = Math.Clamp((clamped - Math.Min(m_aComp[i], travel)) / dt, -4, 4);

				wheelForce = spring * clamped + damper * rate;
				if (comp > travel)
					wheelForce += spring * 6 * (comp - travel); // bump stop

				wheelForce = Math.Max(wheelForce, 0);
			}

			m_aComp[i] = comp;
			maxComp = Math.Max(maxComp, comp);
			force += wheelForce;
			torque += wheelForce * side;
		}

		// body points that hit the ground
		vector up = Vector(0, 1, 0);
		vector fwdP = heading * Math.Cos(m_fPitch) + up * Math.Sin(m_fPitch);
		fwdP.Normalize();
		vector rightP = up * fwdP;
		rightP.Normalize();
		vector upP = fwdP * rightP;

		float cosRoll = Math.Cos(m_fRoll);
		vector rightR = rightP * cosRoll + upP * sinRoll;
		vector upR = upP * cosRoll - rightP * sinRoll;

		int pointCount = m_TrailerComp.GetContactCount();
		if (m_aPointGround.Count() != pointCount)
		{
			m_aPointGround.Clear();
			m_aPointPen.Clear();
			for (int i = 0; i < pointCount; i++)
			{
				m_aPointGround.Insert(NO_GROUND);
				m_aPointPen.Insert(0);
			}
		}

		float contactSpring = bogieMass * GRAVITY / 0.03;
		float contactDamper = 1.4 * Math.Sqrt(contactSpring * bogieMass);
		bool contact = false;
		for (int i = 0; i < pointCount; i++)
		{
			vector local = m_TrailerComp.GetContactLocal(i);
			vector pos = kingpin + rightR * local[0] + upR * local[1] + fwdP * local[2];

			// one ground check per frame is enough here
			if (firstStep)
				m_aPointGround[i] = GroundY(tractor, trailer, pos, pos[1]);

			float pen = m_aPointGround[i] - pos[1];
			float penPrev = m_aPointPen[i];
			m_aPointPen[i] = pen;
			if (pen <= 0)
				continue;

			contact = true;

			float rate = 0;
			if (penPrev > 0)
				rate = Math.Clamp((pen - penPrev) / dt, -4, 4);

			float pointForce = contactSpring * pen + contactDamper * rate;
			if (pointForce <= 0)
				continue;

			// a point near the kingpin hardly lifts the bogie end
			force += pointForce * Math.Clamp(-local[2] / length, 0, 1.3);
			torque += pointForce * vector.Dot(pos - kingpin, right);
		}

		m_fAxleVel += (force / bogieMass - GRAVITY) * dt;
		m_fAxleY += m_fAxleVel * dt;

		// got stuck in the ground or something went wrong, start over
		if (maxComp > 1 || Math.AbsFloat(m_fAxleVel) > 25 || Math.AbsFloat(m_fRollVel) > 20)
		{
			ResetState(tractor, kingpin, tractorFwd, length, radius, wheelCount);
			return;
		}

		// pitch limit of the fifth wheel
		float maxPitch = m_fMaxPitch * Math.DEG2RAD;
		float pitch = Math.Atan2(kingpin[1] - m_fAxleY, length) - restAngle;
		float relPitch = pitch - tractorPitch;
		if (relPitch > maxPitch || relPitch < -maxPitch)
		{
			pitch = tractorPitch + Math.Clamp(relPitch, -maxPitch, maxPitch);
			m_fAxleY = kingpin[1] - length * Math.Tan(pitch + restAngle);
			m_fAxleVel = 0;
		}

		if (halfTrack < 0.1)
			return;

		// roll, the fifth wheel pulls it back to the tractor's roll
		float inertia = bogieMass * halfTrack * halfTrack * 1.5;
		float rollSpring = mass * GRAVITY * halfTrack * 3;
		float rollDamper = 1.2 * Math.Sqrt(rollSpring * inertia);
		torque += rollSpring * (tractorRoll - m_fRoll) - rollDamper * m_fRollVel;

		m_fRollVel += torque / inertia * dt;
		m_fRoll += m_fRollVel * dt;

		float maxRoll = m_fMaxRoll * Math.DEG2RAD;
		if (contact)
			maxRoll = 40 * Math.DEG2RAD;

		float relRoll = m_fRoll - tractorRoll;
		if (relRoll > maxRoll || relRoll < -maxRoll)
		{
			m_fRoll = tractorRoll + Math.Clamp(relRoll, -maxRoll, maxRoll);
			m_fRollVel = 0;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Start from where the trailer is now (after coupling or when we became the owner)
	protected void InitState(IEntity tractor, IEntity trailer, vector kingpin, vector fwd, float length, float radius, int wheelCount)
	{
		vector origin = trailer.GetOrigin();
		vector trailerFwd = trailer.GetTransformAxis(2);
		trailerFwd[1] = 0;
		if (vector.Distance(origin, kingpin) > 3 || trailerFwd.LengthSq() < 0.0001)
		{
			ResetState(tractor, kingpin, fwd, length, radius, wheelCount);
			return;
		}

		trailerFwd.Normalize();
		m_vHeading = trailerFwd;
		m_vBogie = origin - trailerFwd * length;

		vector axle = trailer.CoordToParent(Vector(0, m_TrailerComp.GetAxleY(), -length));
		m_fAxleY = axle[1];
		m_fAxleVel = 0;

		vector axisX = trailer.GetTransformAxis(0);
		m_fRoll = Math.Asin(Math.Clamp(axisX[1], -1, 1));
		m_fRollVel = 0;

		// while driving the trailer still stands where the truck was before this frame's physics,
		// that is no offset to ease out
		m_vKingOffset = vector.Zero;
		if (GetSpeed() < 1)
			m_vKingOffset = origin - kingpin;

		if (DIAG)
			m_iDiagInits++;

		m_aComp.Clear();
		m_aWheelGround.Clear();
		for (int i = 0; i < wheelCount; i++)
		{
			m_aComp.Insert(m_TrailerComp.GetRestCompression());
		}

		m_bHasState = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void ResetState(IEntity tractor, vector kingpin, vector fwd, float length, float radius, int wheelCount)
	{
		m_vBogie = kingpin - fwd * length;
		m_vHeading = fwd;

		float ground = GroundY(tractor, GetTrailer(), m_vBogie, kingpin[1]);
		if (ground <= NO_GROUND)
			ground = kingpin[1] - 1.2;

		m_fAxleY = ground + radius;
		m_fAxleVel = 0;

		vector axisX = tractor.GetTransformAxis(0);
		m_fRoll = Math.Asin(Math.Clamp(axisX[1], -1, 1));
		m_fRollVel = 0;
		m_vKingOffset = vector.Zero;
		if (DIAG)
			m_iDiagInits++;

		m_aComp.Clear();
		m_aWheelGround.Clear();
		for (int i = 0; i < wheelCount; i++)
		{
			m_aComp.Insert(m_TrailerComp.GetRestCompression());
		}

		m_bHasState = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyPose(IEntity tractor, IEntity trailer)
	{
		if (!m_bHasState)
			return;

		vector up = Vector(0, 1, 0);
		vector fwd = m_vHeading * Math.Cos(m_fPitch) + up * Math.Sin(m_fPitch);
		fwd.Normalize();
		vector right = up * fwd;
		right.Normalize();
		up = fwd * right;

		float c = Math.Cos(m_fRoll);
		float s = Math.Sin(m_fRoll);

		vector mat[4];
		mat[0] = right * c + up * s;
		mat[1] = up * c - right * s;
		mat[2] = fwd;
		mat[3] = tractor.CoordToParent(m_vFifthWheel) + m_vKingOffset;
		trailer.SetWorldTransform(mat);
		trailer.Update();

		foreach (int i, float comp : m_aComp)
		{
			m_TrailerComp.SetSimCompression(i, comp);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected float GroundY(IEntity tractor, IEntity trailer, vector pos, float nearY)
	{
		TraceParam param = new TraceParam();
		param.Start = Vector(pos[0], nearY + 1.5, pos[2]);
		param.End = Vector(pos[0], nearY - 3, pos[2]);
		param.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		param.LayerMask = EPhysicsLayerDefs.VehicleCast;

		s_TraceTractor = tractor;
		s_TraceTrailer = trailer;
		s_TraceTowed = null;
		if (m_TrailerComp)
			s_TraceTowed = m_TrailerComp.GetTowedTrailer();
		BaseWorld world = tractor.GetWorld();
		float frac = world.TraceMove(param, TraceFilter);
		if (frac >= 1)
		{
			// start may be under the surface, try again from higher up
			param.Start = Vector(pos[0], nearY + 8, pos[2]);
			frac = world.TraceMove(param, TraceFilter);
			if (frac >= 1)
				return NO_GROUND;
		}

		return param.Start[1] + (param.End[1] - param.Start[1]) * frac;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool TraceFilter(notnull IEntity e)
	{
		// the dolly (or trailer) our trailer tows sits right behind it, under its rear end
		IEntity root = e.GetRootParent();
		if (root == s_TraceTractor || root == s_TraceTrailer || (s_TraceTowed && root == s_TraceTowed))
			return false;

		return !ChimeraCharacter.Cast(root);
	}

	//------------------------------------------------------------------------------------------------
	protected static float SignedAngle(vector a, vector b)
	{
		return Math.Atan2(a[2] * b[0] - a[0] * b[2], a[0] * b[0] + a[2] * b[2]);
	}

	//------------------------------------------------------------------------------------------------
	protected static vector RotateY(vector v, float angle)
	{
		float c = Math.Cos(angle);
		float s = Math.Sin(angle);
		return Vector(v[0] * c + v[2] * s, 0, v[2] * c - v[0] * s);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Unreliable, RplRcver.Server)]
	protected void RpcAsk_SetRelative(vector relPos, vector relAng)
	{
		m_vRelPos = relPos;
		m_vRelAng = relAng;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	protected void GetRelativePose(IEntity tractor, IEntity trailer, out vector relPos, out vector relAng)
	{
		vector tractorMat[4];
		vector trailerMat[4];
		vector rel[4];
		tractor.GetWorldTransform(tractorMat);
		trailer.GetWorldTransform(trailerMat);
		Math3D.MatrixInvMultiply4(tractorMat, trailerMat, rel);

		vector rot[3];
		rot[0] = rel[0];
		rot[1] = rel[1];
		rot[2] = rel[2];

		relPos = rel[3];
		relAng = Math3D.MatrixToAngles(rot);
	}

	//------------------------------------------------------------------------------------------------
	protected void FollowRelativePose(IEntity tractor, IEntity trailer, float timeSlice)
	{
		if (!m_bRelInit)
		{
			m_vRelPosCur = m_vRelPos;
			m_vRelAngCur = m_vRelAng;
			m_bRelInit = true;
		}

		float t = Math.Clamp(timeSlice * 12, 0, 1);
		m_vRelPosCur += (m_vRelPos - m_vRelPosCur) * t;
		m_vRelAngCur += (m_vRelAng - m_vRelAngCur) * t;

		vector rot[3];
		Math3D.AnglesToMatrix(m_vRelAngCur, rot);

		vector rel[4];
		rel[0] = rot[0];
		rel[1] = rot[1];
		rel[2] = rot[2];
		rel[3] = m_vRelPosCur;

		vector tractorMat[4];
		vector world[4];
		tractor.GetWorldTransform(tractorMat);
		Math3D.MatrixMultiply4(tractorMat, rel, world);
		trailer.SetWorldTransform(world);
		trailer.Update();
	}
}
