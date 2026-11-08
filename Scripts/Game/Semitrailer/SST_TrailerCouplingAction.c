// Release handle at the front of the trailer. Uncouples the trailer, or couples it by hand when a
// tractor stands under the kingpin (normally the fifth wheel locks by itself when backing under).
class SST_TrailerCouplingAction : ScriptedUserAction
{
	[Attribute("1", UIWidgets.EditBox, "How far the kingpin may be off the fifth wheel for coupling by hand [m]")]
	protected float m_fCoupleRadius;

	protected SST_SemiTrailerComponent m_Trailer;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_Trailer = SST_SemiTrailerComponent.Cast(pOwnerEntity.FindComponent(SST_SemiTrailerComponent));
	}

	//------------------------------------------------------------------------------------------------
	protected SST_SemiTractorComponent GetCoupledTractor()
	{
		if (!m_Trailer)
			return null;

		IEntity tractor = m_Trailer.GetTractor();
		if (!tractor)
			return null;

		return SST_SemiTractorComponent.Cast(tractor.FindComponent(SST_SemiTractorComponent));
	}

	//------------------------------------------------------------------------------------------------
	protected SST_SemiTractorComponent GetTractor()
	{
		SST_SemiTractorComponent tractor = GetCoupledTractor();
		if (!tractor && m_Trailer)
			tractor = SST_SemiTractorComponent.FindTractorFor(m_Trailer.GetOwner(), m_fCoupleRadius);

		return tractor;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return GetTractor() != null;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		SST_SemiTractorComponent tractor = GetTractor();
		if (!tractor)
			return false;

		if (!GetCoupledTractor() && m_Trailer.IsWrecked())
		{
			SetCannotPerformReason("Trailer is destroyed");
			return false;
		}

		if (!tractor.CanCoupleNow())
		{
			SetCannotPerformReason("Tractor is moving");
			return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		if (GetCoupledTractor())
			outName = "Uncouple trailer";
		else
			outName = "Couple trailer";

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!Replication.IsServer())
			return;

		SST_SemiTractorComponent tractor = GetCoupledTractor();
		if (tractor)
		{
			if (tractor.CanCoupleNow())
				tractor.Uncouple();

			return;
		}

		tractor = GetTractor();
		if (tractor && tractor.CanCoupleNow())
			tractor.Couple(pOwnerEntity);
	}

	//------------------------------------------------------------------------------------------------
	override bool HasLocalEffectOnlyScript()
	{
		return false;
	}
}
