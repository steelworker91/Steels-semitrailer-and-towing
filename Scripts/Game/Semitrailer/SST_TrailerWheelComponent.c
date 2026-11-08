// Trailer wheel. Rolls with the distance the root entity travels and can be moved up/down by the
// trailer suspension. It has the vanilla wheel hit zone; shot flat it gives the suspension a smaller
// radius, like SCR_WheelHitZone does for the wheel simulation of a vehicle.

[ComponentEditorProps(category: "SST/Semitrailer", description: "Visual trailer wheel")]
class SST_TrailerWheelComponentClass : ScriptComponentClass
{
}

class SST_TrailerWheelComponent : ScriptComponent
{
	[Attribute("0.6", UIWidgets.EditBox, "Rolling radius [m]")]
	protected float m_fRadius;

	[Attribute("0.9", UIWidgets.EditBox, "Radius multiplier of a punctured tyre (vanilla SCR_WheelHitZone default)")]
	protected float m_fPuncturedRadiusScale;

	[Attribute("0.8", UIWidgets.EditBox, "Radius multiplier of a destroyed tyre (vanilla SCR_WheelHitZone default)")]
	protected float m_fDestroyedRadiusScale;

	protected DamageManagerComponent m_Damage;

	protected vector m_aBase[4];
	protected bool m_bBaseSet;
	protected vector m_vLastPos;
	protected bool m_bLastSet;
	protected float m_fAngle;
	protected float m_fSuspOffset;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (System.IsConsoleApp())
			return;

		SetEventMask(owner, EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	float GetRadiusScale()
	{
		if (!m_Damage)
			m_Damage = DamageManagerComponent.Cast(GetOwner().FindComponent(DamageManagerComponent));

		if (!m_Damage)
			return 1;

		HitZone hitZone = m_Damage.GetDefaultHitZone();
		if (!hitZone)
			return 1;

		EDamageState state = hitZone.GetDamageState();
		if (state == EDamageState.DESTROYED)
			return m_fDestroyedRadiusScale;

		if (state == SCR_EWheelDamageState.PUNCTURED)
			return m_fPuncturedRadiusScale;

		return 1;
	}

	//------------------------------------------------------------------------------------------------
	//! Offset of the wheel centre from its rest position, in parent space [m]
	void SetSuspensionOffset(float offset)
	{
		m_fSuspOffset = offset;
	}

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		IEntity root = owner.GetRootParent();
		if (!root || root == owner || m_fRadius <= 0.01)
			return;

		if (!m_bBaseSet)
		{
			owner.GetLocalTransform(m_aBase);
			m_bBaseSet = true;
		}

		vector pos = root.GetOrigin();
		if (!m_bLastSet)
		{
			m_vLastPos = pos;
			m_bLastSet = true;
			return;
		}

		vector delta = pos - m_vLastPos;
		m_vLastPos = pos;
		if (delta.LengthSq() > 4) // teleported
			return;

		float dist = vector.Dot(delta, root.GetTransformAxis(2));

		// left side wheels are mounted turned around
		vector baseFwd = m_aBase[2];
		float side = 1;
		if (baseFwd[2] < 0)
			side = -1;

		m_fAngle += side * dist / m_fRadius;
		if (m_fAngle > Math.PI2 || m_fAngle < -Math.PI2)
			m_fAngle = Math.Repeat(m_fAngle, Math.PI2);

		float c = Math.Cos(m_fAngle);
		float s = Math.Sin(m_fAngle);

		vector mat[4];
		mat[0] = m_aBase[0];
		mat[1] = m_aBase[1] * c + m_aBase[2] * s;
		mat[2] = m_aBase[2] * c - m_aBase[1] * s;
		mat[3] = m_aBase[3] + Vector(0, m_fSuspOffset, 0);
		owner.SetLocalTransform(mat);
	}
}
