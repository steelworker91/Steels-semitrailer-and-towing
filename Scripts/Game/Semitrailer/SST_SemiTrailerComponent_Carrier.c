// The vehicles on a carrier deck count into the mass of the trailer (trailer suspension), and they can be
// strapped down (SST_CargoStrapComponent of the framework). A vehicle that comes off goes over the side
// of the deck and falls from there.

modded class SST_SemiTrailerComponent
{
	protected SST_VehicleCarrierComponent m_Carrier;
	protected bool m_bCarrierChecked;

	//------------------------------------------------------------------------------------------------
	override float GetExtraLoadMass()
	{
		float mass = super.GetExtraLoadMass();

		SST_VehicleCarrierComponent carrier = GetCarrier();
		if (carrier)
			mass += carrier.GetLoadedMass();

		return mass;
	}

	//------------------------------------------------------------------------------------------------
	//! Vehicles have their centre of mass about a metre above their wheels (vanilla LAV-25: 1.12 m)
	override float GetLoadHeight()
	{
		SST_VehicleCarrierComponent carrier = GetCarrier();
		if (carrier)
			return carrier.GetDeckY() + 1;

		return super.GetLoadHeight();
	}

	//------------------------------------------------------------------------------------------------
	override float GetRatedLoad()
	{
		float load = super.GetRatedLoad();

		SST_VehicleCarrierComponent carrier = GetCarrier();
		if (carrier)
			load += carrier.GetMaxMass();

		return load;
	}

	//------------------------------------------------------------------------------------------------
	SST_VehicleCarrierComponent GetCarrier()
	{
		if (!m_bCarrierChecked)
		{
			m_Carrier = SST_VehicleCarrierComponent.Cast(GetOwner().FindComponent(SST_VehicleCarrierComponent));
			m_bCarrierChecked = true;
		}

		return m_Carrier;
	}

	//------------------------------------------------------------------------------------------------
	override void GetStrapLoads(notnull array<IEntity> loads)
	{
		super.GetStrapLoads(loads);

		SST_VehicleCarrierComponent carrier = GetCarrier();
		if (carrier)
			carrier.GetLoadedVehicles(loads);
	}

	//------------------------------------------------------------------------------------------------
	override bool DropStrapLoad(IEntity load, float side, vector velocity)
	{
		SST_VehicleCarrierComponent carrier = GetCarrier();
		if (carrier && carrier.ThrowOff(load, side, velocity))
			return true;

		return super.DropStrapLoad(load, side, velocity);
	}
}
