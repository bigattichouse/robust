"""
Unit tests for enhanced experiment functionality.
"""

import tempfile
import pytest
from unittest.mock import Mock, patch

from taguchi.config import TaguchiConfig
from taguchi.core_enhanced import Taguchi
from taguchi.experiment_enhanced import Experiment, _validate_factor_name, _validate_levels
from taguchi.errors import TaguchiError, ValidationError


class TestFactorValidation:
    """Test factor validation functions."""
    
    def test_validate_factor_name_success(self):
        """Test successful factor name validation."""
        assert _validate_factor_name("temperature") == []
        assert _validate_factor_name("TEMP") == []
        assert _validate_factor_name("temp_1") == []
        assert _validate_factor_name("factor123") == []
    
    def test_validate_factor_name_failures(self):
        """Test factor name validation failures."""
        # Empty name
        errors = _validate_factor_name("")
        assert "Factor name must not be empty" in errors[0]
        
        # Non-string
        errors = _validate_factor_name(123)
        assert "Factor name must be a string" in errors[0]
        
        # Invalid characters
        invalid_names = ["temp=value", "temp space", "temp#comment", "temp:colon", "temp,comma"]
        for name in invalid_names:
            errors = _validate_factor_name(name)
            assert len(errors) > 0
            assert "invalid character" in errors[0]
        
        # Too long
        long_name = "a" * 51
        errors = _validate_factor_name(long_name)
        assert "too long" in errors[0]
        
        # Starts with underscore
        errors = _validate_factor_name("_private")
        assert "cannot start with underscore" in errors[0]
    
    def test_validate_levels_success(self):
        """Test successful level validation."""
        assert _validate_levels("temp", ["low", "medium", "high"]) == []
        assert _validate_levels("factor", ["1", "2"]) == []
        assert _validate_levels("single", ["only"]) == []
    
    def test_validate_levels_failures(self):
        """Test level validation failures."""
        # Empty levels
        errors = _validate_levels("factor", [])
        assert "must have at least one level" in errors[0]
        
        # Too many levels
        many_levels = [str(i) for i in range(11)]
        errors = _validate_levels("factor", many_levels)
        assert "too many levels" in errors[0]
        
        # Non-string levels
        errors = _validate_levels("factor", ["valid", 123, "also_valid"])
        assert "must be a string" in errors[0]
        
        # Empty/whitespace levels
        errors = _validate_levels("factor", ["valid", "", "  "])
        assert len([e for e in errors if "empty or whitespace" in e]) == 2
        
        # Duplicate levels
        errors = _validate_levels("factor", ["low", "high", "low"])
        assert "duplicate level" in errors[0]
        
        # Invalid characters
        errors = _validate_levels("factor", ["low", "high,medium", "normal"])
        assert "invalid characters" in errors[0]


class TestExperimentEnhanced:
    """Test the enhanced Experiment class."""
    
    @pytest.fixture
    def mock_taguchi(self):
        """Create a mock Taguchi instance for testing."""
        mock = Mock(spec=Taguchi)
        mock._get_arrays_info.return_value = [
            {'name': 'L9', 'rows': 9, 'cols': 4, 'levels': 3},
            {'name': 'L16', 'rows': 16, 'cols': 5, 'levels': 4},
        ]
        mock.suggest_array.return_value = 'L9'
        mock.generate_runs.return_value = [
            {"run_id": 1, "factors": {"A": "1", "B": "low"}},
            {"run_id": 2, "factors": {"A": "2", "B": "high"}},
        ]
        return mock
    
    def test_init_default(self, mock_taguchi):
        """Test default initialization."""
        with patch('taguchi.experiment_enhanced.Taguchi', return_value=mock_taguchi):
            exp = Experiment()
            assert exp._taguchi is not None
            assert exp._array_type is None
            assert exp._factors == {}
    
    def test_init_with_dependency_injection(self, mock_taguchi):
        """Test initialization with dependency injection."""
        exp = Experiment(taguchi=mock_taguchi)
        assert exp._taguchi is mock_taguchi
    
    def test_init_with_config(self):
        """Test initialization with configuration."""
        config = TaguchiConfig(debug_mode=True)
        
        with patch('taguchi.experiment_enhanced.Taguchi') as mock_taguchi_class:
            exp = Experiment(config=config)
            
            # Should pass config to Taguchi constructor
            mock_taguchi_class.assert_called_once_with(config=config)
    
    def test_add_factor_success(self, mock_taguchi):
        """Test successful factor addition."""
        exp = Experiment(taguchi=mock_taguchi)
        
        result = exp.add_factor("temperature", ["low", "high"])
        
        # Should return self for chaining
        assert result is exp
        assert exp._factors["temperature"] == ["low", "high"]
    
    def test_add_factor_validation_failure(self, mock_taguchi):
        """Test factor addition with validation failure."""
        exp = Experiment(taguchi=mock_taguchi)
        
        # Invalid factor name
        with pytest.raises(ValidationError) as exc_info:
            exp.add_factor("", ["low", "high"])
        
        assert "Factor name must not be empty" in str(exc_info.value)
        
        # Invalid levels
        with pytest.raises(ValidationError) as exc_info:
            exp.add_factor("temp", [])
        
        assert "must have at least one level" in str(exc_info.value)
    
    def test_add_factor_after_generation(self, mock_taguchi):
        """Test that adding factors after generation fails."""
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "high"])
        
        # Generate runs
        exp.generate()
        
        # Now adding factors should fail
        with pytest.raises(TaguchiError, match="Cannot add factors after runs are generated"):
            exp.add_factor("pressure", ["low", "high"])
    
    def test_remove_factor(self, mock_taguchi):
        """Test factor removal."""
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "high"])
        exp.add_factor("pressure", ["low", "high"])
        
        result = exp.remove_factor("temp")
        
        assert result is exp
        assert "temp" not in exp._factors
        assert "pressure" in exp._factors
    
    def test_remove_nonexistent_factor(self, mock_taguchi):
        """Test removing non-existent factor."""
        exp = Experiment(taguchi=mock_taguchi)
        
        with pytest.raises(TaguchiError, match="Factor 'nonexistent' not found"):
            exp.remove_factor("nonexistent")
    
    def test_clear_factors(self, mock_taguchi):
        """Test clearing all factors."""
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "high"])
        exp.add_factor("pressure", ["low", "high"])
        
        result = exp.clear_factors()
        
        assert result is exp
        assert exp._factors == {}
    
    def test_validate_success(self, mock_taguchi):
        """Test successful experiment validation."""
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "high"])
        exp.add_factor("pressure", ["low", "high"])
        
        errors = exp.validate()
        assert errors == []
        assert exp.is_valid()
    
    def test_validate_no_factors(self, mock_taguchi):
        """Test validation with no factors."""
        exp = Experiment(taguchi=mock_taguchi)
        
        errors = exp.validate()
        assert "No factors defined" in errors[0]
        assert not exp.is_valid()
    
    def test_validate_array_incompatibility(self, mock_taguchi):
        """Test validation with array incompatibility."""
        # Mock arrays that don't support the experiment
        mock_taguchi._get_arrays_info.return_value = [
            {'name': 'L4', 'rows': 4, 'cols': 2, 'levels': 2},  # Too few columns and levels
        ]
        
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "medium", "high"])  # 3 levels
        exp.add_factor("pressure", ["low", "high"])        # 2 factors total, but need 3 levels
        exp.add_factor("humidity", ["low", "high"])        # 3 factors total
        
        errors = exp.validate()
        assert len(errors) > 0
        assert any("No available array supports" in error for error in errors)
    
    def test_validate_explicit_array_mismatch(self, mock_taguchi):
        """Test validation with explicit array that doesn't fit."""
        mock_taguchi.get_array_info.return_value = {
            'rows': 4, 'cols': 2, 'levels': 2
        }
        
        exp = Experiment(array_type="L4", taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "medium", "high"])  # 3 levels, but L4 only supports 2
        exp.add_factor("pressure", ["low", "high"])
        exp.add_factor("humidity", ["low", "high"])        # 3 factors, but L4 only has 2 columns
        
        errors = exp.validate()
        assert len(errors) >= 2  # Should have errors for both columns and levels
        assert any("only 2 columns" in error for error in errors)
        assert any("only 2 levels" in error for error in errors)
    
    def test_validate_and_raise(self, mock_taguchi):
        """Test validate_and_raise method."""
        exp = Experiment(taguchi=mock_taguchi)
        
        # Should raise with no factors
        with pytest.raises(ValidationError):
            exp.validate_and_raise()
        
        # Should not raise with valid factors
        exp.add_factor("temp", ["low", "high"])
        exp.validate_and_raise()  # Should not raise
    
    def test_factor_count_property(self, mock_taguchi):
        """Test factor_count property."""
        exp = Experiment(taguchi=mock_taguchi)
        assert exp.factor_count == 0
        
        exp.add_factor("temp", ["low", "high"])
        assert exp.factor_count == 1
        
        exp.add_factor("pressure", ["low", "high"])
        assert exp.factor_count == 2
    
    def test_max_levels_property(self, mock_taguchi):
        """Test max_levels property."""
        exp = Experiment(taguchi=mock_taguchi)
        assert exp.max_levels == 0
        
        exp.add_factor("temp", ["low", "high"])  # 2 levels
        assert exp.max_levels == 2
        
        exp.add_factor("pressure", ["low", "medium", "high"])  # 3 levels
        assert exp.max_levels == 3
    
    def test_estimate_runtime(self, mock_taguchi):
        """Test runtime estimation."""
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "high"])
        
        runtime = exp.estimate_runtime(seconds_per_run=60.0)
        
        assert runtime["total_runs"] == 2  # From mock
        assert runtime["seconds_per_run"] == 60.0
        assert runtime["total_seconds"] == 120.0
        assert runtime["total_minutes"] == 2.0
        assert runtime["total_hours"] == 2.0/60
    
    def test_compare_with_full_factorial(self, mock_taguchi):
        """Test full factorial comparison."""
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "high"])      # 2 levels
        exp.add_factor("pressure", ["low", "high"])  # 2 levels
        
        comparison = exp.compare_with_full_factorial()
        
        assert comparison["taguchi_runs"] == 2  # From mock
        assert comparison["full_factorial_runs"] == 4  # 2 * 2
        assert comparison["efficiency_ratio"] == 0.5
        assert comparison["runs_saved"] == 2
        assert comparison["percentage_reduction"] == 50.0
    
    def test_string_representations(self, mock_taguchi):
        """Test string and repr methods."""
        exp = Experiment(taguchi=mock_taguchi)
        
        # Empty experiment
        assert "Empty Experiment" in str(exp)
        
        # With factors
        exp.add_factor("temp", ["low", "high"])
        exp.add_factor("pressure", ["low", "high"])
        
        exp_str = str(exp)
        assert "Taguchi Experiment (2 factors)" in exp_str
        assert "temp: low, high" in exp_str
        assert "pressure: low, high" in exp_str
        
        exp_repr = repr(exp)
        assert "Experiment(factors=2" in exp_repr
        assert "runs=2" in exp_repr
    
    def test_from_tgu_with_dependency_injection(self, mock_taguchi):
        """Test loading from TGU with dependency injection."""
        tgu_content = """
factors:
  temperature: low, medium, high
  pressure: low, high
array: L9
"""
        
        with tempfile.NamedTemporaryFile(mode='w', suffix='.tgu', delete=False) as f:
            f.write(tgu_content)
            tgu_path = f.name
        
        try:
            exp = Experiment.from_tgu(tgu_path, taguchi=mock_taguchi)
            
            assert exp._taguchi is mock_taguchi
            assert "temperature" in exp._factors
            assert "pressure" in exp._factors
            assert exp._factors["temperature"] == ["low", "medium", "high"]
            assert exp._factors["pressure"] == ["low", "high"]
            assert exp._array_type == "L9"
        finally:
            import os
            os.unlink(tgu_path)
    
    def test_from_tgu_validation_errors(self, mock_taguchi):
        """Test TGU loading with validation errors."""
        tgu_content = """
factors:
  invalid name: low, high
  temp: 
"""
        
        with tempfile.NamedTemporaryFile(mode='w', suffix='.tgu', delete=False) as f:
            f.write(tgu_content)
            tgu_path = f.name
        
        try:
            with pytest.raises(ValidationError):
                Experiment.from_tgu(tgu_path, taguchi=mock_taguchi)
        finally:
            import os
            os.unlink(tgu_path)


class TestExperimentIntegration:
    """Integration tests for enhanced experiment functionality."""
    
    def test_validation_caching(self):
        """Test that validation results are cached appropriately."""
        mock_taguchi = Mock(spec=Taguchi)
        mock_taguchi._get_arrays_info.return_value = [
            {'name': 'L9', 'rows': 9, 'cols': 4, 'levels': 3},
        ]
        
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "high"])
        
        # First validation should call _get_arrays_info
        errors1 = exp.validate()
        assert mock_taguchi._get_arrays_info.call_count == 1
        
        # Second validation should use cache
        errors2 = exp.validate()
        assert mock_taguchi._get_arrays_info.call_count == 1  # No additional call
        assert errors1 == errors2
        
        # Adding factor should invalidate cache
        exp.add_factor("pressure", ["low", "high"])
        errors3 = exp.validate()
        assert mock_taguchi._get_arrays_info.call_count == 2  # New call made
    
    def test_configuration_propagation(self):
        """Test that configuration propagates correctly."""
        config = TaguchiConfig(debug_mode=True, cli_timeout=120)
        
        with patch('taguchi.experiment_enhanced.Taguchi') as mock_taguchi_class:
            mock_instance = Mock()
            mock_taguchi_class.return_value = mock_instance
            
            exp = Experiment(config=config)
            
            # Should create Taguchi with config
            mock_taguchi_class.assert_called_once_with(config=config)
            assert exp._taguchi is mock_instance
    
    def test_backward_compatibility(self):
        """Test backward compatibility with original API."""
        with patch('taguchi.experiment_enhanced.Taguchi') as mock_taguchi_class:
            mock_instance = Mock()
            mock_taguchi_class.return_value = mock_instance
            
            # Original constructor pattern should still work
            exp = Experiment(array_type="L9")
            
            assert exp._array_type == "L9"
            assert exp._taguchi is mock_instance
            
            # Original methods should still work
            exp.add_factor("temp", ["low", "high"])
            assert "temp" in exp._factors
    
    def test_error_propagation(self):
        """Test that errors from Taguchi are properly propagated."""
        mock_taguchi = Mock(spec=Taguchi)
        mock_taguchi._get_arrays_info.side_effect = TaguchiError("CLI error")
        
        exp = Experiment(taguchi=mock_taguchi)
        exp.add_factor("temp", ["low", "high"])
        
        errors = exp.validate()
        assert len(errors) > 0
        assert any("Error validating array compatibility" in error for error in errors)