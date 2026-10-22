/////////////////////////////////////////////////////////////////////////////////
// Paint.NET                                                                   //
// Copyright (C) Rick Brewster, Tom Jackson, and past contributors.            //
// Portions Copyright (C) Microsoft Corporation. All Rights Reserved.          //
// See src/Resources/Files/License.txt for full licensing and attribution      //
// details.                                                                    //
// .                                                                           //
/////////////////////////////////////////////////////////////////////////////////

using PaintDotNet.PropertySystem;
using System;
using System.Collections.Generic;

namespace PaintDotNet.IndirectUI
{
    public abstract class ControlInfo
        : ICloneable<ControlInfo>
    {
        private PropertyCollection controlProperties;
        private List<ControlInfo> childControls = new List<ControlInfo>();

        public PropertyCollection ControlProperties
        {
            get
            {
                return this.controlProperties;
            }

            protected set
            {
                this.controlProperties = value.Clone();
            }
        }

        public IList<ControlInfo> ChildControls
        {
            get
            {
                return new List<ControlInfo>(this.childControls);
            }
        }

        private static PropertyControlInfo FindControlForPropertyName(object propertyName, ControlInfo control)
        {
            PropertyControlInfo asPCI = control as PropertyControlInfo;

            if (asPCI != null && asPCI.Property.Name == propertyName.ToString())
            {
                return asPCI;
            }
            else
            {
                foreach (ControlInfo childControl in control.GetChildControlsCore())
                {
                    PropertyControlInfo pci = FindControlForPropertyName(propertyName, childControl);

                    if (pci != null)
                    {
                        return pci;
                    }
                }
            }

            return null;
        }

        public PropertyControlInfo FindControlForPropertyName(object propertyName)
        {
            return FindControlForPropertyName(propertyName, this);
        }

        public bool SetPropertyControlValue(object propertyName, object controlPropertyName, object propertyValue)
        {
            PropertyControlInfo pci = FindControlForPropertyName(propertyName);

            if (pci == null)
            {
                return false;
            }

            // Kaint: tolerante con nombres o tipos que no conoce (Paint.NET 4.x
            // añadió propiedades de control nuevas).
            return pci.SetControlValue(controlPropertyName, propertyValue);
        }

        public bool SetPropertyControlType(object propertyName, PropertyControlType newControlType)
        {
            PropertyControlInfo pci = FindControlForPropertyName(propertyName);

            if (pci == null)
            {
                return false;
            }

            if (-1 == Array.IndexOf(pci.ControlType.ValueChoices, newControlType))
            {
                return false;
            }

            pci.ControlType.Value = newControlType;
            return true;
        }

        protected List<ControlInfo> GetChildControlsCore()
        {
            return this.childControls;
        }

        internal ControlInfo()
        {
            this.controlProperties = new PropertyCollection(new Property[0], new PropertyCollectionRule[0]);
        }

        internal ControlInfo(PropertyCollection controlProperties)
        {
            this.controlProperties = controlProperties.Clone();
        }

        internal ControlInfo(ControlInfo cloneMe)
        {
            this.controlProperties = cloneMe.controlProperties.Clone();
            this.childControls = new List<ControlInfo>(cloneMe.childControls.Count);

            foreach (ControlInfo ci in cloneMe.childControls)
            {
                ControlInfo ciClone = ci.Clone();
                this.childControls.Add(ciClone);
            }
        }

        // Kaint: el diálogo lo construye Kaint a partir de esta descripción.
        public object CreateConcreteControl(object uiContainer)
        {
            throw new NotSupportedException("Kaint construye los controles de IndirectUI por su cuenta");
        }

        public object CreateConcreteControl(Type uiContainerType)
        {
            throw new NotSupportedException("Kaint construye los controles de IndirectUI por su cuenta");
        }

        public abstract ControlInfo Clone();

        object ICloneable.Clone()
        {
            return Clone();
        }
    }
}
