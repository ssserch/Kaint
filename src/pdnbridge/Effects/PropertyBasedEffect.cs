/////////////////////////////////////////////////////////////////////////////////
// Paint.NET                                                                   //
// Copyright (C) Rick Brewster, Tom Jackson, and past contributors.            //
// Portions Copyright (C) Microsoft Corporation. All Rights Reserved.          //
// See src/Resources/Files/License.txt for full licensing and attribution      //
// details.                                                                    //
// .                                                                           //
/////////////////////////////////////////////////////////////////////////////////

using PaintDotNet.IndirectUI;
using PaintDotNet.PropertySystem;
using System;
using System.Collections.Generic;
using System.Drawing;

namespace PaintDotNet.Effects
{
    public abstract class PropertyBasedEffect
        : Effect<PropertyBasedEffectConfigToken>
    {
        protected abstract PropertyCollection OnCreatePropertyCollection();

        public PropertyCollection CreatePropertyCollection()
        {
            return OnCreatePropertyCollection();
        }

        protected virtual ControlInfo OnCreateConfigUI(PropertyCollection props)
        {
            return CreateDefaultConfigUI(props);
        }

        public ControlInfo CreateConfigUI(PropertyCollection props)
        {
            PropertyCollection props2 = props.Clone();

            using (props2.__Internal_BeginEventAddMoratorium())
            {
                ControlInfo configUI1 = OnCreateConfigUI(props2);
                ControlInfo configUI2 = configUI1.Clone();
                return configUI2;
            }
        }

        public static ControlInfo CreateDefaultConfigUI(IEnumerable<Property> props)
        {
            PanelControlInfo configUI = new PanelControlInfo();

            foreach (Property property in props)
            {
                PropertyControlInfo propertyControlInfo = PropertyControlInfo.CreateFor(property);
                propertyControlInfo.ControlProperties[ControlInfoPropertyNames.DisplayName].Value = property.Name;
                configUI.AddChildControl(propertyControlInfo);
            }

            return configUI;
        }

        private string GetConfigDialogTitle()
        {
            return this.Name;
        }

        protected virtual void OnCustomizeConfigUIWindowProperties(PropertyCollection props)
        {
            return;
        }

        // Kaint: el diálogo lo construye Kaint con la descripción de IndirectUI
        // (KaintCreateConfigUI y KaintCreateWindowProperties).
        public override sealed EffectConfigDialog CreateConfigDialog()
        {
            return null;
        }

        public ControlInfo KaintCreateConfigUI(PropertyCollection props)
        {
            return CreateConfigUI(props);
        }

        public PropertyCollection KaintCreateWindowProperties()
        {
            List<Property> props = new List<Property>();
            props.Add(new StringProperty(ControlInfoPropertyNames.WindowTitle, this.Name ?? string.Empty, 1024));
            props.Add(new BooleanProperty(ControlInfoPropertyNames.WindowIsSizable, false));
            props.Add(new DoubleProperty(ControlInfoPropertyNames.WindowWidthScale, 1.0, 1.0, 2.0));
            props.Add(new StringProperty(ControlInfoPropertyNames.WindowHelpContent, string.Empty, StringProperty.MaxMaxLength));
            props.Add(new ObjectProperty(ControlInfoPropertyNames.WindowHelpContentType, WindowHelpContentType.None));
            PropertyCollection windowProps = PropertyCollection.CreateLenient(props);
            OnCustomizeConfigUIWindowProperties(windowProps);
            return windowProps;
        }

        protected PropertyBasedEffect(string name, Image image, string subMenuName, EffectFlags flags)
            : base(name, image, subMenuName, flags)
        {
        }

        // Kaint: constructor de Paint.NET 4.3 y posteriores.
        protected PropertyBasedEffect(string name, Image image, string subMenuName, EffectOptions options)
            : base(name, image, subMenuName, options)
        {
        }
    }
}
