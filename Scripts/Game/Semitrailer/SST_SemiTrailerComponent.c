// Trailer side of the semitrailer. While coupled the tractor (SST_SemiTractorComponent) moves the
// trailer, this component holds the trailer data: wheel layout, suspension settings, cargo,
// collision layers, wheel visuals, tyre tracks and the tail lights.
// Uncoupled, the server puts the trailer down on its wheels and landing legs ("parked") and
// replicates that pose.

[ComponentEditorProps(category: "SST/Semitrailer", description: "Semitrailer, towed by SST_SemiTractorComponent")]
class SST_SemiTrailerComponentClass : ScriptComponentClass
{
}

class SST_SemiTrailerComponent : ScriptComponent
{
	[Attribute("-0.6", UIWidgets.EditBox, "Wheel centre height at rest, relative to the kingpin [m]", category: "Suspension")]
	protected float m_fAxleY;

	[Attribute("0.598", UIWidgets.EditBox, "Wheel radius [m]", category: "Suspension")]
	protected float m_fWheelRadius;

	[Attribute("0.2", UIWidgets.EditBox, "Suspension travel [m]", category: "Suspension")]
	protected float m_fTravel;

	[Attribute("0.08", UIWidgets.EditBox, "Compression at rest [m]", category: "Suspension")]
	protected float m_fRestCompression;

	[Attribute("0.55", UIWidgets.EditBox, "Part of the weight carried by the axles, the rest is on the kingpin", category: "Suspension")]
	protected float m_fAxleShare;

	[Attribute("7000", UIWidgets.EditBox, "Empty mass [kg]", category: "Cargo")]
	protected float m_fEmptyMass;

	[Attribute("0", UIWidgets.EditBox, "Extra mass while something sits in the cargo slot [kg]", category: "Cargo")]
	protected float m_fCargoMass;

	[Attribute("Cargo", UIWidgets.EditBox, "SlotManager slot that holds the cargo", category: "Cargo")]
	protected string m_sCargoSlot;

	[Attribute("", UIWidgets.EditBox, "Optional: child entities with this component also count as cargo (e.g. cargo that was loaded back by another mod)", category: "Cargo")]
	protected string m_sCargoComponent;

	[Attribute("UBX_cargo", UIWidgets.EditBox, "Collider used for the cargo, disabled while there is no cargo", category: "Cargo")]
	protected string m_sCargoGeom;

	[Attribute("0 1.735 -5.4", UIWidgets.Coords, "Cargo box centre, trailer space. Its top corners can touch the ground when the trailer tips over", category: "Cargo")]
	protected vector m_vCargoCenter;

	[Attribute("2.6 2.47 10", UIWidgets.Coords, "Cargo box size", category: "Cargo")]
	protected vector m_vCargoSize;

	[Attribute("UBX_frame UBX_cargo", UIWidgets.EditBox, "Colliders above the tractor, these don't collide with vehicles", category: "Collision")]
	protected string m_sNoVehicleGeoms;

	[Attribute("0.665 -1.09 -2", UIWidgets.Coords, "Foot of the right landing leg, trailer space (left one is mirrored)", category: "Parking")]
	protected vector m_vLegFoot;

	[Attribute("0", UIWidgets.CheckBox, "Drawbar trailer (dolly): the origin is the drawbar eye, couples to a tow hitch instead of a fifth wheel", category: "Coupling")]
	protected bool m_bDrawbar;

	[Attribute(desc: "Points of the body that must not go into the ground, trailer space. Empty: the ones of the flatbed semitrailer", category: "Suspension")]
	protected ref array<vector> m_aGroundPoints;

	[Attribute("{31DD25B5E8A3BB73}Assets/Decals/Dirt/TireTracks/Data/Decal_Tracks_01.emat", UIWidgets.ResourceNamePicker, "Tyre track material", params: "emat", category: "Tracks")]
	protected ResourceName m_sTrackMaterial;

	[Attribute("0.38", UIWidgets.EditBox, "Tyre track width [m]", category: "Tracks")]
	protected float m_fTrackWidth;

	[Attribute("120", UIWidgets.EditBox, "Track lifetime [s]", category: "Tracks")]
	protected float m_fTrackLifetime;

	[Attribute("0.8", UIWidgets.EditBox, "Track opacity", category: "Tracks")]
	protected float m_fTrackAlpha;

	[RplProp(onRplName: "OnParkChanged")]
	protected bool m_bParked;

	[RplProp(onRplName: "OnParkChanged")]
	protected vector m_vParkPos;

	[RplProp(onRplName: "OnParkChanged")]
	protected vector m_vParkAng;

	// cargo put (back) on the trailer at runtime, e.g. a C-130 load taken off the aircraft
	[RplProp(onRplName: "OnLoadedCargoChanged")]
	protected RplId m_LoadedCargoId;

	protected IEntity m_Tractor;
	protected IEntity m_BlockedTractor;
	protected float m_fIdle;
	protected float m_fNodeTimer;
	protected vector m_vNodePos;
	protected bool m_bNodePosSet;
	protected bool m_bStreamingOff;
	protected bool m_bApplyPark;
	protected BaseLightManagerComponent m_LightManager;
	protected bool m_bSimulated;
	protected bool m_bHasCargo;
	protected int m_iCargoState = -1;
	protected typename m_CargoType;

	protected bool m_bLayersInit;
	protected ref array<int> m_aGeomMasks = {};
	protected int m_iCargoGeom = -1;

	protected bool m_bWheelsFound;
	protected float m_fParkedTyres;
	protected float m_fKingpinToBogie = 9.6;
	protected ref array<vector> m_aWheelPos = {};
	protected ref array<SST_TrailerWheelComponent> m_aWheels = {};
	protected ref array<float> m_aComp = {};
	protected ref array<ref TrackDecalFactory> m_aTracks = {};
	protected vector m_vLastPos;
	protected bool m_bLastPosSet;

	// points of the body that should not go into the ground (trailer space)
	protected ref array<vector> m_aBodyPoints = {
		Vector(-1.27, 0, 1.1), Vector(1.27, 0, 1.1),
		Vector(-1.27, 0, -5), Vector(1.27, 0, -5),
		Vector(-1.27, 0, -10.95), Vector(1.27, 0, -10.95),
		Vector(-1.1, -0.6, -11.08), Vector(1.1, -0.6, -11.08),
		Vector(-0.665, -1.09, -2), Vector(0.665, -1.09, -2)
	};
	protected ref array<vector> m_aCargoPoints = {};

	protected static IEntity s_TraceSelf;
	protected static IEntity s_TraceTractor;
	protected static IEntity s_TraceTowed;
	protected static ref array<SST_SemiTrailerComponent> s_aTrailers = {};

	static const float NO_GROUND = -1000000;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame() || !GetGame().InPlayMode())
			return;

		// top corners of the cargo box
		float hx = m_vCargoSize[0] * 0.5;
		float hz = m_vCargoSize[2] * 0.5;
		float top = m_vCargoCenter[1] + m_vCargoSize[1] * 0.5;
		m_aCargoPoints.Insert(Vector(m_vCargoCenter[0] - hx, top, m_vCargoCenter[2] + hz));
		m_aCargoPoints.Insert(Vector(m_vCargoCenter[0] + hx, top, m_vCargoCenter[2] + hz));
		m_aCargoPoints.Insert(Vector(m_vCargoCenter[0] - hx, top, m_vCargoCenter[2] - hz));
		m_aCargoPoints.Insert(Vector(m_vCargoCenter[0] + hx, top, m_vCargoCenter[2] - hz));

		if (m_aGroundPoints && !m_aGroundPoints.IsEmpty())
			m_aBodyPoints.Copy(m_aGroundPoints);

		s_aTrailers.Insert(this);
		SetEventMask(owner, EntityEvent.FRAME);
		GetGame().GetCallqueue().CallLater(FreezeCargo, 0, false);
		GetGame().GetCallqueue().CallLater(UpdateCargo, 500, true);

		if (Replication.IsServer())
		{
			// placed on its own (editor) -> put it down, a tractor that spawned it couples it before that
			GetGame().GetCallqueue().CallLater(ParkIfFree, 500, false);
			GetGame().GetCallqueue().CallLater(CheckParked, 1000, true);
		}
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(UpdateCargo);
			GetGame().GetCallqueue().Remove(FreezeCargo);
			GetGame().GetCallqueue().Remove(ParkIfFree);
			GetGame().GetCallqueue().Remove(CheckParked);
		}

		s_aTrailers.RemoveItem(this);
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	static array<SST_SemiTrailerComponent> GetAll()
	{
		return s_aTrailers;
	}

	//------------------------------------------------------------------------------------------------
	void SetTractor(IEntity tractor)
	{
		m_Tractor = tractor;
		if (!tractor)
		{
			m_bSimulated = false;
			m_fIdle = 0;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server, called by the tractor
	void OnCoupled(IEntity tractor)
	{
		SetTractor(tractor);
		m_BlockedTractor = null;
		m_bParked = false;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! Server, called by the tractor
	void OnUncoupled(IEntity tractor)
	{
		SetTractor(null);
		SwitchLightsOff();

		// don't lock again while the tractor is still under the trailer
		m_BlockedTractor = tractor;
		Park();
	}

	//------------------------------------------------------------------------------------------------
	bool IsBlockedFor(IEntity tractor)
	{
		return m_BlockedTractor && m_BlockedTractor == tractor;
	}

	//------------------------------------------------------------------------------------------------
	void ClearBlock()
	{
		m_BlockedTractor = null;
	}

	//------------------------------------------------------------------------------------------------
	bool IsParked()
	{
		return m_bParked;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnParkChanged()
	{
		m_fIdle = 0;
		m_bApplyPark = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void ParkIfFree()
	{
		if (!m_Tractor)
			Park();
	}

	//------------------------------------------------------------------------------------------------
	//! Server: put it down again when someone (game master) moved it
	protected void CheckParked()
	{
		if (m_Tractor || !m_bParked)
			return;

		IEntity owner = GetOwner();
		vector mat[4];
		owner.GetWorldTransform(mat);

		vector parkRot[3];
		Math3D.AnglesToMatrix(m_vParkAng, parkRot);
		if (vector.Distance(mat[3], m_vParkPos) > 0.05 || vector.Dot(mat[2], parkRot[2]) < 0.9999 || vector.Dot(mat[1], parkRot[1]) < 0.9999)
			Park();
		else if (!float.AlmostEqual(GetTyreState(), m_fParkedTyres))
			Park(); // a tyre went flat (or was repaired)
	}

	//------------------------------------------------------------------------------------------------
	//! Server: stand the trailer on its wheels and landing legs where it is now
	void Park()
	{
		IEntity owner = GetOwner();
		if (!owner || !Replication.IsServer())
			return;

		vector mat[4];
		if (!GetParkedPose(owner, mat))
			owner.GetWorldTransform(mat);

		owner.SetWorldTransform(mat);
		owner.Update();

		m_vParkPos = mat[3];
		m_vParkAng = GetAngles(mat);
		m_fParkedTyres = GetTyreState();
		m_bParked = true;
		m_fIdle = 0;
		EnsurePhysics();
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	protected bool GetParkedPose(IEntity owner, out vector mat[4])
	{
		if (!m_bWheelsFound)
			FindWheels(owner);

		float halfTrack = 0;
		foreach (vector wheel : m_aWheelPos)
		{
			halfTrack = Math.Max(halfTrack, Math.AbsFloat(wheel[0]));
		}

		if (halfTrack < 0.1)
			return false;

		vector fwd = owner.GetTransformAxis(2);
		fwd[1] = 0;
		if (fwd.LengthSq() < 0.0001)
			fwd = vector.Forward;

		fwd.Normalize();
		vector right = Vector(fwd[2], 0, -fwd[0]);
		vector kingpin = owner.GetOrigin();
		float length = m_fKingpinToBogie;
		vector bogie = kingpin - fwd * length;

		float groundL = GroundY(owner, bogie - right * halfTrack, kingpin[1]);
		float groundR = GroundY(owner, bogie + right * halfTrack, kingpin[1]);
		if (groundL <= NO_GROUND && groundR <= NO_GROUND)
			return false;

		if (groundL <= NO_GROUND)
			groundL = groundR;
		else if (groundR <= NO_GROUND)
			groundR = groundL;

		vector legs = kingpin + fwd * m_vLegFoot[2];
		float legL = GroundY(owner, legs - right * m_vLegFoot[0], kingpin[1]);
		float legR = GroundY(owner, legs + right * m_vLegFoot[0], kingpin[1]);
		float legGround = Math.Max(legL, legR);
		if (legGround <= NO_GROUND)
			legGround = (groundL + groundR) * 0.5;

		// right is +x of the trailer (the side the right vector points to)
		float axleL = groundL + GetSideRadius(-1);
		float axleR = groundR + GetSideRadius(1);
		float axleY = (axleL + axleR) * 0.5;
		float roll = Math.Atan2(axleR - axleL, 2 * halfTrack);
		float restAngle = Math.Atan2(-m_fAxleY, length);

		// the leg foot moves a little with the pitch, two passes are enough
		float pitch = 0;
		float kingY = kingpin[1];
		for (int i = 0; i < 2; i++)
		{
			kingY = legGround - (m_vLegFoot[1] * Math.Cos(pitch) + m_vLegFoot[2] * Math.Sin(pitch));
			pitch = Math.Atan2(kingY - axleY, length) - restAngle;
		}

		kingpin[1] = kingY;
		MakePose(fwd, pitch, roll, kingpin, mat);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Trailer matrix from heading (horizontal), pitch (+ = nose up), roll (+ = right side up) and kingpin position
	static void MakePose(vector heading, float pitch, float roll, vector origin, out vector mat[4])
	{
		vector up = Vector(0, 1, 0);
		vector fwd = heading * Math.Cos(pitch) + up * Math.Sin(pitch);
		fwd.Normalize();
		vector right = up * fwd;
		right.Normalize();
		up = fwd * right;

		float c = Math.Cos(roll);
		float s = Math.Sin(roll);
		mat[0] = right * c + up * s;
		mat[1] = up * c - right * s;
		mat[2] = fwd;
		mat[3] = origin;
	}

	//------------------------------------------------------------------------------------------------
	protected static vector GetAngles(vector mat[4])
	{
		vector rot[3];
		rot[0] = mat[0];
		rot[1] = mat[1];
		rot[2] = mat[2];
		return Math3D.MatrixToAngles(rot);
	}

	//------------------------------------------------------------------------------------------------
	protected float GroundY(IEntity owner, vector pos, float nearY)
	{
		TraceParam param = new TraceParam();
		param.Start = Vector(pos[0], nearY + 4, pos[2]);
		param.End = Vector(pos[0], nearY - 6, pos[2]);
		param.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		param.LayerMask = EPhysicsLayerDefs.VehicleCast;

		s_TraceSelf = owner;
		s_TraceTowed = GetTowedTrailer();
		float frac = owner.GetWorld().TraceMove(param, ParkFilter);
		if (frac >= 1)
			return NO_GROUND;

		return param.Start[1] + (param.End[1] - param.Start[1]) * frac;
	}

	//------------------------------------------------------------------------------------------------
	//! Ground only: the tractor may still stand under the legs, a dolly has its trailer on top
	protected static bool ParkFilter(notnull IEntity e)
	{
		IEntity root = e.GetRootParent();
		if (root == s_TraceSelf || (s_TraceTowed && root == s_TraceTowed))
			return false;

		return !ChimeraCharacter.Cast(root) && !Vehicle.Cast(root);
	}

	//------------------------------------------------------------------------------------------------
	//! Kinematic body that only collides, on every machine
	protected void EnsurePhysics()
	{
		Physics phys = GetOwner().GetPhysics();
		if (!phys)
			return;

		if (phys.GetSimulationState() != SimulationState.COLLISION)
			phys.ChangeSimulationState(SimulationState.COLLISION);

		if (!m_bLayersInit)
			InitLayers(phys);
	}

	//------------------------------------------------------------------------------------------------
	IEntity GetTractor()
	{
		return m_Tractor;
	}

	//------------------------------------------------------------------------------------------------
	bool HasDrawbar()
	{
		return m_bDrawbar;
	}

	//------------------------------------------------------------------------------------------------
	//! Road train: the trailer this one tows with its own hitch or fifth wheel
	IEntity GetTowedTrailer()
	{
		SST_SemiTractorComponent hitch = SST_SemiTractorComponent.Cast(GetOwner().FindComponent(SST_SemiTractorComponent));
		if (!hitch)
			return null;

		return hitch.GetTrailer();
	}

	//------------------------------------------------------------------------------------------------
	//! Called by the tractor every frame. The body is kinematic on every machine,
	//! the tractor poses it.
	void SetSimulated(bool simulated)
	{
		m_bSimulated = simulated;
		EnsurePhysics();
	}

	//------------------------------------------------------------------------------------------------
	int GetWheelCount()
	{
		if (!m_bWheelsFound)
			FindWheels(GetOwner());

		return m_aWheelPos.Count();
	}

	vector GetWheelLocal(int i) { return m_aWheelPos[i]; }
	float GetKingpinToBogie() { return m_fKingpinToBogie; }
	float GetAxleY() { return m_fAxleY; }
	float GetWheelRadius() { return m_fWheelRadius; }

	//------------------------------------------------------------------------------------------------
	//! Radius of one wheel, smaller when its tyre is shot flat
	float GetWheelRadiusAt(int i)
	{
		if (m_aWheels.IsIndexValid(i) && m_aWheels[i])
			return m_fWheelRadius * m_aWheels[i].GetRadiusScale();

		return m_fWheelRadius;
	}

	//------------------------------------------------------------------------------------------------
	//! Average wheel radius of one side (side < 0 left, > 0 right)
	protected float GetSideRadius(float side)
	{
		float sum = 0;
		int count = 0;
		for (int i = 0, n = m_aWheelPos.Count(); i < n; i++)
		{
			if (m_aWheelPos[i][0] * side <= 0)
				continue;

			sum += GetWheelRadiusAt(i);
			count++;
		}

		if (count == 0)
			return m_fWheelRadius;

		return sum / count;
	}

	//------------------------------------------------------------------------------------------------
	protected float GetTyreState()
	{
		float sum = 0;
		for (int i = 0, n = m_aWheels.Count(); i < n; i++)
		{
			sum += GetWheelRadiusAt(i) * (i + 1);
		}

		return sum;
	}

	//------------------------------------------------------------------------------------------------
	//! Destroyed trailers can't be coupled any more
	bool IsWrecked()
	{
		DamageManagerComponent damage = DamageManagerComponent.Cast(GetOwner().FindComponent(DamageManagerComponent));
		return damage && damage.GetState() == EDamageState.DESTROYED;
	}
	float GetRestCompression() { return m_fRestCompression; }
	float GetTravel() { return m_fTravel; }
	float GetAxleShare() { return m_fAxleShare; }

	//------------------------------------------------------------------------------------------------
	float GetMass()
	{
		float mass = m_fEmptyMass;
		if (m_bHasCargo)
			mass += m_fCargoMass;

		return mass + GetExtraLoadMass();
	}

	//------------------------------------------------------------------------------------------------
	//! Load that is not in the cargo slot (vehicles on a carrier deck and the like), add-ons override it
	float GetExtraLoadMass()
	{
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	int GetContactCount()
	{
		if (m_bHasCargo)
			return m_aBodyPoints.Count() + m_aCargoPoints.Count();

		return m_aBodyPoints.Count();
	}

	//------------------------------------------------------------------------------------------------
	vector GetContactLocal(int i)
	{
		if (i < m_aBodyPoints.Count())
			return m_aBodyPoints[i];

		return m_aCargoPoints[i - m_aBodyPoints.Count()];
	}

	//------------------------------------------------------------------------------------------------
	//! Suspension compression of wheel i from the tractor's simulation (negative = in the air)
	void SetSimCompression(int i, float c)
	{
		if (i < m_aComp.Count())
			m_aComp[i] = c;
	}

	//------------------------------------------------------------------------------------------------
	protected void FindWheels(IEntity owner)
	{
		m_bWheelsFound = true;
		bool tracks = !System.IsConsoleApp() && !m_sTrackMaterial.IsEmpty();
		float sumZ = 0;

		IEntity child = owner.GetChildren();
		while (child)
		{
			SST_TrailerWheelComponent wheel = SST_TrailerWheelComponent.Cast(child.FindComponent(SST_TrailerWheelComponent));
			if (wheel)
			{
				vector mat[4];
				child.GetLocalTransform(mat);
				vector pos = mat[3];

				m_aWheelPos.Insert(Vector(pos[0], m_fAxleY, pos[2]));
				m_aWheels.Insert(wheel);
				m_aComp.Insert(m_fRestCompression);
				m_aTracks.Insert(null);
				sumZ += pos[2];
			}

			child = child.GetSibling();
		}

		// the wheels on one side run in the same rut, only the rearmost one leaves a track
		if (tracks)
		{
			foreach (int i, vector wheelPos : m_aWheelPos)
			{
				bool rearmost = true;
				foreach (vector other : m_aWheelPos)
				{
					if (other[0] * wheelPos[0] > 0 && other[2] < wheelPos[2])
					{
						rearmost = false;
						break;
					}
				}

				if (rearmost)
					m_aTracks[i] = new TrackDecalFactory(m_sTrackMaterial, m_fTrackWidth, m_fTrackLifetime);
			}
		}

		if (!m_aWheelPos.IsEmpty())
			m_fKingpinToBogie = -sumZ / m_aWheelPos.Count();
	}

	//------------------------------------------------------------------------------------------------
	//! Wheel visuals and tyre tracks. Proxies cast their own rays for the wheel height,
	//! the simulating machine gets it from the tractor.
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		if (Replication.IsServer())
			UpdateNode(owner, timeSlice);

		if (!m_Tractor)
		{
			if (!m_bLayersInit)
				EnsurePhysics();

			// clients take the parked pose from the server
			if (m_bParked && !Replication.IsServer() && (m_bApplyPark || vector.DistanceSq(owner.GetOrigin(), m_vParkPos) > 0.0001))
			{
				m_bApplyPark = false;
				vector rot[3];
				Math3D.AnglesToMatrix(m_vParkAng, rot);
				vector mat[4];
				mat[0] = rot[0];
				mat[1] = rot[1];
				mat[2] = rot[2];
				mat[3] = m_vParkPos;
				owner.SetWorldTransform(mat);
				owner.Update();
				m_fIdle = 0;
			}
		}

		if (System.IsConsoleApp())
			return;

		if (!m_bWheelsFound)
			FindWheels(owner);

		int count = m_aWheelPos.Count();
		if (count == 0)
			return;

		vector pos = owner.GetOrigin();
		float speed = 0;
		if (m_bLastPosSet && timeSlice > 0)
			speed = vector.Distance(pos, m_vLastPos) / timeSlice;

		m_vLastPos = pos;
		m_bLastPosSet = true;

		// standing trailer, wheels are already on the ground
		if (!m_Tractor && speed < 0.01)
		{
			m_fIdle += timeSlice;
			if (m_fIdle > 2)
				return;
		}
		else
		{
			m_fIdle = 0;
		}

		float topOffset = m_fTravel - m_fRestCompression;
		float len = m_fTravel + m_fWheelRadius + 0.3;
		vector down = -owner.GetTransformAxis(1);

		vector hitPos, hitNorm;
		IEntity hitEnt;
		for (int i = 0; i < count; i++)
		{
			vector start = owner.CoordToParent(m_aWheelPos[i] + Vector(0, topOffset, 0));
			float dist = CastDown(owner, start, down, len, hitPos, hitNorm, hitEnt);

			float comp = m_aComp[i];
			if (!m_bSimulated)
			{
				float target = -1;
				if (dist >= 0)
					target = m_fTravel + GetWheelRadiusAt(i) - dist;

				comp = Math.Lerp(comp, target, Math.Clamp(timeSlice * 20, 0, 1));
				m_aComp[i] = comp;
			}

			if (m_aWheels[i])
				m_aWheels[i].SetSuspensionOffset(Math.Clamp(comp, 0, m_fTravel) - m_fRestCompression);

			TrackDecalFactory track = m_aTracks[i];
			if (!track)
				continue;

			if (dist >= 0 && comp >= -0.02 && speed > 0.3 && speed < 60)
				track.AddPoint(hitEnt, hitPos + hitNorm * 0.01, hitNorm, m_fTrackAlpha);
			else if (track.HasActiveDecal())
				track.FinalizeDecal(0.3);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The trailer is moved without networked movement, so the replication scheduler has to be told
	//! where it is now (otherwise a towed trailer streams out where it was placed).
	//! While coupled the trailer is not streamed at all: with its node moved every second the driver's
	//! truck jumped sideways once a second at speed (with ping). The node is moved once after
	//! uncoupling instead.
	protected void UpdateNode(IEntity owner, float timeSlice)
	{
		RplComponent rpl = RplComponent.Cast(owner.FindComponent(RplComponent));
		if (!rpl)
			return;

		if (m_Tractor)
		{
			if (!m_bStreamingOff)
			{
				rpl.EnableStreaming(false);
				m_bStreamingOff = true;
			}

			return;
		}

		if (m_bStreamingOff)
		{
			if (m_bNodePosSet)
				rpl.ForceNodeMovement(m_vNodePos);

			m_vNodePos = owner.GetOrigin();
			m_bNodePosSet = true;
			rpl.EnableStreaming(true);
			m_bStreamingOff = false;
			m_fNodeTimer = 0;
			return;
		}

		m_fNodeTimer += timeSlice;
		if (m_fNodeTimer < 1)
			return;

		m_fNodeTimer = 0;
		vector pos = owner.GetOrigin();
		if (!m_bNodePosSet)
		{
			m_vNodePos = pos;
			m_bNodePosSet = true;
			return;
		}

		if (vector.DistanceSq(pos, m_vNodePos) < 1)
			return;

		rpl.ForceNodeMovement(m_vNodePos);
		m_vNodePos = pos;
	}

	//------------------------------------------------------------------------------------------------
	protected float CastDown(IEntity owner, vector start, vector dir, float len, out vector hitPos, out vector hitNorm, out IEntity hitEnt)
	{
		TraceParam param = new TraceParam();
		param.Start = start;
		param.End = start + dir * len;
		param.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		param.LayerMask = EPhysicsLayerDefs.VehicleCast;

		s_TraceSelf = owner;
		s_TraceTractor = m_Tractor;
		float frac = owner.GetWorld().TraceMove(param, TraceFilter);
		if (frac >= 1)
			return -1;

		hitPos = param.Start + (param.End - param.Start) * frac;
		hitNorm = param.TraceNorm;
		hitEnt = param.TraceEnt;
		return len * frac;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool TraceFilter(notnull IEntity e)
	{
		IEntity root = e.GetRootParent();
		if (root == s_TraceSelf || (s_TraceTractor && root == s_TraceTractor))
			return false;

		return !ChimeraCharacter.Cast(root);
	}

	//------------------------------------------------------------------------------------------------
	protected void InitLayers(Physics phys)
	{
		m_bLayersInit = true;

		int geomCount = phys.GetNumGeoms();
		for (int i = 0; i < geomCount; i++)
		{
			m_aGeomMasks.Insert(phys.GetGeomInteractionLayer(i));
		}

		// the front of the trailer sits over the tractor, keep vehicles out of those colliders
		array<string> names = {};
		m_sNoVehicleGeoms.Split(" ", names, true);
		int vehicleLayer = EPhysicsLayerDefs.VehicleSimple;
		foreach (string name : names)
		{
			int idx = phys.GetGeom(name);
			if (idx >= 0)
				m_aGeomMasks[idx] = m_aGeomMasks[idx] & ~vehicleLayer;
		}

		m_iCargoGeom = phys.GetGeom(m_sCargoGeom);
		ApplyLayers(phys);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyLayers(Physics phys)
	{
		for (int i = 0; i < m_aGeomMasks.Count(); i++)
		{
			int mask = m_aGeomMasks[i];
			if (i == m_iCargoGeom && !m_bHasCargo)
				mask = 0;

			phys.SetGeomInteractionLayer(i, mask);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Cargo = the entity in the cargo slot, as long as it is still attached to the trailer,
	//! plus any child that has m_sCargoComponent.
	protected void GetCargo(notnull array<IEntity> cargo)
	{
		IEntity owner = GetOwner();
		IEntity slotEntity;
		SlotManagerComponent slots = SlotManagerComponent.Cast(owner.FindComponent(SlotManagerComponent));
		if (slots)
		{
			EntitySlotInfo slot = slots.GetSlotByName(m_sCargoSlot);
			if (slot)
				slotEntity = slot.GetAttachedEntity();
		}

		if (!m_CargoType && !m_sCargoComponent.IsEmpty())
			m_CargoType = m_sCargoComponent.ToType();

		IEntity child = owner.GetChildren();
		while (child)
		{
			if (child == slotEntity)
				cargo.Insert(child);
			else if (m_CargoType && child.FindComponent(m_CargoType))
				cargo.Insert(child);

			child = child.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Cargo brings its own physics, which would push against the trailer colliders
	protected void FreezeCargo()
	{
		array<IEntity> cargo = {};
		GetCargo(cargo);
		Freeze(cargo);
	}

	//------------------------------------------------------------------------------------------------
	protected void Freeze(notnull array<IEntity> entities)
	{
		foreach (IEntity entity : entities)
		{
			Physics phys = entity.GetPhysics();
			if (phys && phys.GetSimulationState() != SimulationState.NONE)
				phys.ChangeSimulationState(SimulationState.NONE);
		}
	}

	//------------------------------------------------------------------------------------------------
	vector GetCargoCenter() { return m_vCargoCenter; }

	//------------------------------------------------------------------------------------------------
	//! Can this entity go on the deck: it has the cargo component of this trailer and the deck is free
	bool CanTakeCargo(IEntity cargo)
	{
		if (!cargo || cargo.GetParent() || IsWrecked())
			return false;

		if (!m_CargoType && !m_sCargoComponent.IsEmpty())
			m_CargoType = m_sCargoComponent.ToType();

		if (!m_CargoType || !cargo.FindComponent(m_CargoType))
			return false;

		array<IEntity> onDeck = {};
		GetCargo(onDeck);
		return onDeck.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	//! Server: put the cargo on the deck where the cargo slot holds it
	bool LoadCargo(IEntity cargo)
	{
		if (!Replication.IsServer() || !CanTakeCargo(cargo))
			return false;

		RplComponent rpl = RplComponent.Cast(cargo.FindComponent(RplComponent));
		if (!rpl)
			return false;

		m_LoadedCargoId = rpl.Id();
		AttachLoadedCargo();
		Replication.BumpMe();
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnLoadedCargoChanged()
	{
		AttachLoadedCargo();
	}

	//------------------------------------------------------------------------------------------------
	protected void AttachLoadedCargo()
	{
		if (!m_LoadedCargoId.IsValid())
			return;

		RplComponent rpl = RplComponent.Cast(Replication.FindItem(m_LoadedCargoId));
		if (!rpl)
			return;

		IEntity cargo = rpl.GetEntity();
		IEntity owner = GetOwner();
		if (!cargo || cargo.GetParent())
			return;

		Physics phys = cargo.GetPhysics();
		if (phys)
		{
			phys.SetVelocity(vector.Zero);
			phys.SetAngularVelocity(vector.Zero);
			phys.ChangeSimulationState(SimulationState.NONE);
		}

		vector mat[4];
		Math3D.MatrixIdentity4(mat);
		SlotManagerComponent slots = SlotManagerComponent.Cast(owner.FindComponent(SlotManagerComponent));
		if (slots)
		{
			EntitySlotInfo slot = slots.GetSlotByName(m_sCargoSlot);
			if (slot)
				slot.GetLocalTransform(mat);
		}

		vector prevPos = cargo.GetOrigin();
		owner.AddChild(cargo, -1, EAddChildFlags.AUTO_TRANSFORM);
		cargo.SetLocalTransform(mat);
		cargo.Update();

		// moved without networked movement
		if (Replication.IsServer())
			rpl.ForceNodeMovement(prevPos);

		m_iCargoState = -1;
		UpdateCargo();
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateCargo()
	{
		array<IEntity> cargo = {};
		GetCargo(cargo);
		Freeze(cargo);

		if (m_LoadedCargoId.IsValid())
		{
			RplComponent rpl = RplComponent.Cast(Replication.FindItem(m_LoadedCargoId));
			IEntity loaded;
			if (rpl)
				loaded = rpl.GetEntity();

			if (Replication.IsServer())
			{
				// taken off the deck again (into an aircraft, deleted): forget it
				if (!loaded || loaded.GetParent() != GetOwner())
				{
					m_LoadedCargoId = RplId.Invalid();
					Replication.BumpMe();
				}
			}
			else if (loaded && !loaded.GetParent())
			{
				// joined later or streamed in after the change: put it on the deck here too
				AttachLoadedCargo();
				return;
			}
		}

		int state = 0;
		if (!cargo.IsEmpty())
			state = 1;

		if (state == m_iCargoState)
			return;

		m_iCargoState = state;
		m_bHasCargo = state == 1;

		Physics phys = GetOwner().GetPhysics();
		if (phys && m_bLayersInit)
			ApplyLayers(phys);
	}

	//------------------------------------------------------------------------------------------------
	void SwitchLightsOff()
	{
		if (!m_LightManager)
			m_LightManager = BaseLightManagerComponent.Cast(GetOwner().FindComponent(BaseLightManagerComponent));

		if (!m_LightManager)
			return;

		SetLight(ELightType.Presence, -1, false);
		SetLight(ELightType.Brake, -1, false);
		SetLight(ELightType.Hazard, 0, false);
		SetLight(ELightType.Hazard, 1, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Tail lights follow the tractor switches
	void SyncLights(IEntity tractor, BaseLightManagerComponent tractorLights)
	{
		if (!m_LightManager)
			m_LightManager = BaseLightManagerComponent.Cast(GetOwner().FindComponent(BaseLightManagerComponent));

		if (!m_LightManager || !tractorLights)
			return;

		bool brake = tractorLights.GetLightsState(ELightType.Brake);
		if (!brake)
		{
			VehicleWheeledSimulation sim = VehicleWheeledSimulation.Cast(tractor.FindComponent(VehicleWheeledSimulation));
			brake = sim && sim.GetBrake() > 0.1;
		}

		SetLight(ELightType.Presence, -1, tractorLights.GetLightsState(ELightType.Presence));
		SetLight(ELightType.Brake, -1, brake);
		SetLight(ELightType.Hazard, 0, tractorLights.GetLightsState(ELightType.Hazard, 0));
		SetLight(ELightType.Hazard, 1, tractorLights.GetLightsState(ELightType.Hazard, 1));
	}

	//------------------------------------------------------------------------------------------------
	protected void SetLight(ELightType type, int side, bool on)
	{
		if (m_LightManager.GetLightsState(type, side) != on)
			m_LightManager.SetLightsState(type, on, side);
	}
}
