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
