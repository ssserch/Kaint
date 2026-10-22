// Kaint: propiedad sin tipo concreto. La usan las colecciones tolerantes
// (propiedades de la ventana de configuración) para nombres que Paint.NET 4.x
// añadió y que esta capa de compatibilidad no conoce.

namespace PaintDotNet.PropertySystem
{
    public sealed class ObjectProperty
        : Property<object>
    {
        public ObjectProperty(object name, object defaultValue)
            : base(name, defaultValue, false, ValueValidationFailureResult.Ignore)
        {
        }

        private ObjectProperty(ObjectProperty cloneMe, ObjectProperty sentinelNotUsed)
            : base(cloneMe, sentinelNotUsed)
        {
        }

        protected override object OnClampNewValueT(object newValue)
        {
            return newValue;
        }

        protected override bool ValidateNewValueT(object newValue)
        {
            return true;
        }

        public override Property Clone()
        {
            return new ObjectProperty(this, this);
        }
    }
}
