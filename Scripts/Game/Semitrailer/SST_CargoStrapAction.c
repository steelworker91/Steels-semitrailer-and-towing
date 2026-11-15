// "Strap down load" / "Tighten straps" and "Release straps", along the sides of a trailer with
// SST_CargoStrapComponent. They work on everything on the deck and only while the train stands.
// The strap action comes in four kinds (m_iMode): normal straps, straps that are not drawn, straps
// that are drawn and never work loose, and lashing chains (work loose slower than straps).
class SST_CargoStrapAction : ScriptedUserAction
{
	[Attribute("0", UIWidgets.CheckBox, "Takes the straps off instead of putting them on")]
	protected bool m_bRelease;

	[Attribute("0", UIWidgets.ComboBox, "How the load is strapped", enums: { ParamEnum("Normal", "0"), ParamEnum("No visual straps", "1"), ParamEnum("Can't come loose", "2"), ParamEnum("Lashing chains", "3") })]
	protected int m_iMode;

	protected SST_CargoStrapComponent m_Straps;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_Straps = SST_CargoStrapComponent.Cast(pOwnerEntity.FindComponent(SST_CargoStrapComponent));
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		if (!m_Straps)
			return false;

		int loads, secured, inMode, stage;
		m_Straps.GetModeStatus(m_iMode, loads, secured, inMode, stage);
		if (m_bRelease)
			return secured > 0;

		// nothing to do while everything is strapped this way and tight
		return loads > 0 && (inMode < loads || stage > 0);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		if (!m_Straps)
			return false;

		if (!m_Straps.IsStationary())
		{
			SetCannotPerformReason("Trailer is moving");
			return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		if (m_bRelease)
		{
			outName = "Release straps";
			if (m_Straps && m_Straps.HasMode(SST_CargoStrapComponent.STRAP_CHAIN))
			{
				if (HasStraps())
					outName = "Release straps and chains";
				else
					outName = "Release chains";
			}

			return true;
		}

		int loads, secured, inMode, stage;
		if (m_Straps)
			m_Straps.GetModeStatus(m_iMode, loads, secured, inMode, stage);

		if (m_iMode == SST_CargoStrapComponent.STRAP_CHAIN)
		{
			outName = "Tighten chains";
			if (inMode < loads)
				outName = "Lash down with chains";

			return true;
		}

		string kind;
		if (m_iMode == SST_CargoStrapComponent.STRAP_HIDDEN)
			kind = " (no visual straps)";
		else if (m_iMode == SST_CargoStrapComponent.STRAP_FIXED)
			kind = " (can't come loose)";

		outName = "Tighten straps" + kind;
		if (inMode < loads)
			outName = "Strap down load" + kind;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Some load held down with straps of any kind
	protected bool HasStraps()
	{
		return m_Straps.HasMode(SST_CargoStrapComponent.STRAP_NORMAL) || m_Straps.HasMode(SST_CargoStrapComponent.STRAP_HIDDEN) || m_Straps.HasMode(SST_CargoStrapComponent.STRAP_FIXED);
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!Replication.IsServer() || !m_Straps || !m_Straps.IsStationary())
			return;

		if (m_bRelease)
			m_Straps.ReleaseAll();
		else
			m_Straps.SecureAll(m_iMode);
	}

	//------------------------------------------------------------------------------------------------
	override bool HasLocalEffectOnlyScript()
	{
		return false;
	}
}
